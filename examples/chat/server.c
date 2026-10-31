// The chat server: one process per connection, rooms as process groups,
// nicknames in the registry, and an acceptor that owns every socket.
#define _GNU_SOURCE // accept4
#include "chat.h"
#include <api/message.h>
#include <api/memory.h>
#include <api/monitor.h>
#include <api/registry.h>
#include <api/group.h>
#include <api/bin.h>
#include <api/supervisor.h>
#include <api/timer.h>
#include <api/io.h>
#include <api/log.h>
#include <utils/time.h>
#include <arpa/inet.h>
#include <errno.h>
#include <netinet/in.h>
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

#define CHAT_LINE_TAG MAKE_ATOM_TAG("chat::line") // A line to write to a client
#define CHAT_NAME_MAX (CHAT_NICK_MAX + 8)         // "nick::" or "room::" and a name

/*
    Statistics
*/

TASK0(stats_server) {
    REGISTER("chat::stats", self->pid);
    ChatStats total = {0};
    while (1) {
        Message *msg = RECEIVE(NO_TAG);
        if (msg->tag.kind == TAG_KIND_REF) {
            REPLY(msg, &total, sizeof total);
        } else if (MSG_IS(msg, MAKE_ATOM_TAG("chat::count")) && msg->size == sizeof(ChatStats)) {
            const ChatStats *delta = msg->payload;
            total.connections += delta->connections;
            total.broadcasts += delta->broadcasts;
            total.deliveries += delta->deliveries;
            total.dropped += delta->dropped;
            total.slow += delta->slow;
            total.idle += delta->idle;
            total.crashed += delta->crashed;
        }
        MSG_FREE(msg);
    }
}

void chat_count(Process *self, ChatStats delta) {
    Pid stats = WHEREIS("chat::stats");
    if (stats != INVALID_PID)
        SEND(stats, &delta, sizeof delta, MAKE_ATOM_TAG("chat::count"));
}

bool chat_stats(Process *self, ChatStats *out) {
    Pid stats = WHEREIS("chat::stats");
    Message *reply = stats == INVALID_PID ? NULL : REQUEST_REF(stats, NULL, 0, 1000);
    if (!reply)
        return false;
    memcpy(out, reply->payload, sizeof *out);
    MSG_FREE(reply);
    return true;
}

/*
    One connection
*/

typedef struct {
    int fd;
    char nick[CHAT_NICK_MAX + 1]; // Empty until NICK
    char input[2 * CHAT_LINE_MAX];
    size_t input_length;
    char *output; // CHAT_OUTPUT_MAX bytes not yet written to the client
    size_t output_length;
    int armed; // IO_* events of the pending watch, 0 once it fired
    uint64_t last_activity;
} Connection;

typedef enum {
    KEEP_OPEN,
    CLOSE,   // QUIT, or the client went away
    CUT_OFF, // It does not read what it is sent
} Outcome;

// Names of nicknames and rooms: letters, digits and '_'
static bool valid_name(const char *name) {
    size_t length = strlen(name);
    if (length == 0 || length > CHAT_NICK_MAX)
        return false;
    for (const char *c = name; *c; ++c) {
        bool letter = (*c >= 'a' && *c <= 'z') || (*c >= 'A' && *c <= 'Z');
        if (!letter && !(*c >= '0' && *c <= '9') && *c != '_')
            return false;
    }
    return true;
}

// Writes what the socket takes now; false if the client is gone
static bool flush(Connection *c) {
    while (c->output_length > 0) {
        ssize_t written = write(c->fd, c->output, c->output_length);
        if (written < 0)
            return errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR;
        memmove(c->output, c->output + written, c->output_length - (size_t)written);
        c->output_length -= (size_t)written;
        c->last_activity = now_ms();
    }
    return true;
}

// Queues text for the client; false if it already has too much unread
static bool queue(Connection *c, const char *text, size_t length) {
    if (!c->output || c->output_length + length > CHAT_OUTPUT_MAX)
        return false;
    memcpy(c->output + c->output_length, text, length);
    c->output_length += length;
    return flush(c);
}

static bool reply(Connection *c, const char *text) {
    return queue(c, text, strlen(text));
}

// Watches the socket for what the connection waits for: input, and room for output
static void rearm(Process *self, Connection *c) {
    int wanted = IO_READ | (c->output_length > 0 ? IO_WRITE : 0);
    if (c->armed != wanted && IO_WATCH(c->fd, wanted) == 0)
        c->armed = wanted;
}

// Sends line to every member of the room, as one shared binary
static Outcome broadcast(Process *self, Connection *c, const char *room, const char *text) {
    char group[CHAT_NAME_MAX];
    snprintf(group, sizeof group, "room::%s", room);
    size_t count = GROUP_MEMBERS(group, NULL, 0);
    Pid *members = count > 0 ? ALLOC(count * sizeof(Pid)) : NULL;
    // The room may have grown since: only the first count members fit
    size_t now_in = members ? GROUP_MEMBERS(group, members, count) : 0;
    count = now_in < count ? now_in : count;

    bool member = false;
    for (size_t i = 0; i < count; ++i)
        member = member || members[i] == self->pid;
    if (!member) {
        FREE(members);
        return reply(c, "ERR notin\n") ? KEEP_OPEN : CUT_OFF;
    }

    char *line = FORMAT("FROM %s %s %s\n", c->nick, room, text);
    size_t length = line ? strlen(line) : 0;
    void *shared = line ? BIN_ALLOC(length) : NULL; // The bytes of the line, without its NUL
    ChatStats delta = {.broadcasts = 1};
    if (shared) {
        memcpy(shared, line, length);
        for (size_t i = 0; i < count; ++i) {
            int sent = SEND_BIN(members[i], shared, CHAT_LINE_TAG);
            delta.deliveries += sent == 0;
            delta.dropped += sent == SEND_FULL;
        }
        BIN_FREE(shared);
    }
    FREE(line);
    FREE(members);
    chat_count(self, delta);
    return KEEP_OPEN;
}

static Outcome private_message(Process *self, Connection *c, const char *nick, const char *text) {
    char name[CHAT_NAME_MAX];
    snprintf(name, sizeof name, "nick::%s", nick);
    Pid to = valid_name(nick) ? WHEREIS(name) : INVALID_PID;
    if (to == INVALID_PID)
        return reply(c, "ERR nobody\n") ? KEEP_OPEN : CUT_OFF;

    char *line = FORMAT("FROM %s - %s\n", c->nick, text);
    if (line && SEND(to, line, strlen(line), CHAT_LINE_TAG) == 0)
        chat_count(self, (ChatStats){.deliveries = 1});
    FREE(line);
    return KEEP_OPEN;
}

// Splits "WORD rest" in place: returns rest, or "" if there is none
static char *split(char *text) {
    char *space = strchr(text, ' ');
    if (!space)
        return text + strlen(text);
    *space = '\0';
    return space + 1;
}

static Outcome handle_line(Process *self, Connection *c, char *line) {
    char *argument = split(line);
    char *rest = split(argument);
    bool ok = true;

    if (strcmp(line, "NICK") == 0) {
        char name[CHAT_NAME_MAX];
        snprintf(name, sizeof name, "nick::%s", argument);
        if (c->nick[0] || !valid_name(argument))
            ok = reply(c, "ERR invalid\n");
        else if (REGISTER(name, self->pid) != 0) // Atomic: one winner, freed when the process ends
            ok = reply(c, "ERR taken\n");
        else {
            snprintf(c->nick, sizeof c->nick, "%s", argument);
            ok = reply(c, "OK\n");
        }
    } else if (!c->nick[0] && strcmp(line, "QUIT") != 0) {
        ok = reply(c, "ERR nonick\n");
    } else if (strcmp(line, "JOIN") == 0) {
        char group[CHAT_NAME_MAX];
        snprintf(group, sizeof group, "room::%s", argument);
        ok = reply(c, valid_name(argument) && GROUP_JOIN(group) == 0 ? "OK\n" : "ERR invalid\n");
    } else if (strcmp(line, "MSG") == 0) {
        return valid_name(argument) ? broadcast(self, c, argument, rest)
                                    : (reply(c, "ERR invalid\n") ? KEEP_OPEN : CUT_OFF);
    } else if (strcmp(line, "PRIV") == 0) {
        return private_message(self, c, argument, rest);
    } else if (strcmp(line, "WHO") == 0) {
        char group[CHAT_NAME_MAX], answer[CHAT_NAME_MAX + 32];
        snprintf(group, sizeof group, "room::%s", argument);
        snprintf(answer, sizeof answer, "WHO %s %zu\n", argument, GROUP_MEMBERS(group, NULL, 0));
        ok = reply(c, answer);
    } else if (strcmp(line, "QUIT") == 0) {
        reply(c, "BYE\n");
        return CLOSE;
    } else if (strcmp(line, "CRASH") == 0) {
        // A bug in the handling of one client: only this connection dies
        LOG_WARN("%s asked to crash", c->nick);
        EXIT_ERROR();
    } else {
        ok = reply(c, "ERR unknown\n");
    }
    return ok ? KEEP_OPEN : CUT_OFF;
}

// Reads what the client sent and handles each complete line
static Outcome read_input(Process *self, Connection *c) {
    while (1) {
        ssize_t got = read(c->fd, c->input + c->input_length, sizeof c->input - c->input_length);
        if (got == 0)
            return CLOSE;
        if (got < 0)
            return errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR ? KEEP_OPEN : CLOSE;
        c->input_length += (size_t)got;
        c->last_activity = now_ms();

        char *start = c->input, *newline;
        while ((newline = memchr(start, '\n', c->input_length - (size_t)(start - c->input)))) {
            *newline = '\0';
            if (newline > start && newline[-1] == '\r')
                newline[-1] = '\0';
            Outcome outcome = handle_line(self, c, start);
            if (outcome != KEEP_OPEN)
                return outcome;
            start = newline + 1;
        }
        c->input_length -= (size_t)(start - c->input);
        memmove(c->input, start, c->input_length);
        if (c->input_length == sizeof c->input) // A line longer than CHAT_LINE_MAX: drop it
            c->input_length = 0;
    }
}

TASK(connection, int fd, int idle_ms) {
    Connection *c = ALLOC(sizeof *c);
    char *output = ALLOC(CHAT_OUTPUT_MAX);
    if (!c || !output)
        EXIT_ERROR();
    *c = (Connection){.fd = fd, .output = output, .last_activity = now_ms()};
    LOG_INFO("client connected on fd %d", fd);

    Outcome outcome = KEEP_OPEN;
    while (outcome == KEEP_OPEN) {
        rearm(self, c);
        uint64_t idle_for = now_ms() - c->last_activity;
        Message *msg = idle_for < (uint64_t)idle_ms ? RECEIVE_TIMEOUT(NO_TAG, (uint64_t)idle_ms - idle_for) : NULL;

        if (!msg) { // Nothing either way for idle_ms
            reply(c, "ERR idle\n");
            chat_count(self, (ChatStats){.idle = 1});
            outcome = CLOSE;
        } else if (msg->tag.kind == TAG_KIND_FD) { // The socket is ready: one watch, one message
            int ready = *(int *)msg->payload;
            MSG_FREE(msg);
            c->armed = 0;
            if ((ready & IO_WRITE) && !flush(c))
                outcome = CLOSE;
            else if (ready & (IO_READ | IO_HANGUP | IO_ERROR))
                outcome = read_input(self, c);
        } else if (MSG_IS(msg, CHAT_LINE_TAG)) { // A line for this client
            bool queued = queue(c, msg->payload, msg->size);
            MSG_FREE(msg);
            outcome = queued ? KEEP_OPEN : CUT_OFF;
        } else {
            MSG_FREE(msg);
        }
    }

    if (outcome == CUT_OFF) {
        LOG_WARN("%s does not read: cut off", c->nick[0] ? c->nick : "a client");
        chat_count(self, (ChatStats){.slow = 1});
    }
    // Give the last lines (BYE, ERR idle) a moment to go out
    for (int tries = 0; outcome == CLOSE && c->output_length > 0 && tries < 10 && flush(c); ++tries)
        IO_WAIT_TIMEOUT(c->fd, IO_WRITE, 100);
    LOG_INFO("%s left", c->nick[0] ? c->nick : "a client");
    // The acceptor closes the socket when it hears of the end, however it ends
}

/*
    The acceptor: accepts connections and owns their sockets
*/

typedef struct {
    Pid pid;
    int fd;
} Socket;

static Pid start_connection(Process *self, int fd, int idle_ms) {
    Funcall_t call;
    funcall_init(&call, connection, 2, fd, idle_ms);
    return clpf_spawn(self, &call,
                      (SpawnOptions){.name = "chat::conn",
                                     .monitor = true,
                                     .mailbox_limit = CHAT_MAILBOX_LIMIT,
                                     .max_heap_size = CHAT_HEAP_LIMIT});
}

TASK(acceptor, int listener, int idle_ms) {
    size_t capacity = 64, count = 0;
    Socket *sockets = ALLOC(capacity * sizeof(Socket));
    if (!sockets)
        EXIT_ERROR();
    IO_WATCH(listener, IO_READ);

    while (1) {
        Message *msg = RECEIVE(NO_TAG);
        if (msg->tag.kind == TAG_KIND_FD) {
            int fd;
            while ((fd = accept4(listener, NULL, NULL, SOCK_NONBLOCK | SOCK_CLOEXEC)) >= 0) {
                // Bound what a client that does not read can hold, in the kernel too
                int buffer = CHAT_SOCKET_BUFFER;
                setsockopt(fd, SOL_SOCKET, SO_SNDBUF, &buffer, sizeof buffer);
                if (count == capacity) {
                    Socket *grown = ALLOC(2 * capacity * sizeof(Socket));
                    if (!grown) {
                        close(fd);
                        continue;
                    }
                    memcpy(grown, sockets, count * sizeof(Socket));
                    FREE(sockets);
                    sockets = grown;
                    capacity *= 2;
                }
                Pid pid = start_connection(self, fd, idle_ms);
                if (pid == INVALID_PID) {
                    close(fd);
                    continue;
                }
                sockets[count++] = (Socket){pid, fd};
                chat_count(self, (ChatStats){.connections = 1});
            }
            IO_WATCH(listener, IO_READ);
        } else if (msg->tag.kind == TAG_KIND_DOWN) { // A connection ended: close its socket
            const DownInfo *down = msg->payload;
            for (size_t i = 0; i < count; ++i) {
                if (sockets[i].pid != down->from)
                    continue;
                if (down->reason != EXIT_REASON_NORMAL)
                    chat_count(self, (ChatStats){.crashed = 1});
                close(sockets[i].fd);
                sockets[i] = sockets[--count];
                break;
            }
        }
        MSG_FREE(msg);
    }
}

int chat_start(Process *self, int port, bool local, int idle_ms) {
    int listener = socket(AF_INET, SOCK_STREAM | SOCK_NONBLOCK | SOCK_CLOEXEC, 0);
    if (listener < 0)
        return -1;
    int reuse = 1;
    setsockopt(listener, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof reuse);
    struct sockaddr_in address = {
        .sin_family = AF_INET,
        .sin_port = htons((uint16_t)port),
        .sin_addr.s_addr = htonl(local ? INADDR_LOOPBACK : INADDR_ANY),
    };
    socklen_t length = sizeof address;
    if (bind(listener, (struct sockaddr *)&address, length) != 0 || listen(listener, SOMAXCONN) != 0 ||
        getsockname(listener, (struct sockaddr *)&address, &length) != 0) {
        close(listener);
        return -1;
    }

    // The acceptor and the counters are restarted if they fail; the listener outlives them
    Pid sup = SPAWN_SUPERVISOR("chat::sup", SUP_ONE_FOR_ONE, 1000, 5);
    SUP_ADD_CHILD0(sup, RES_PERMANENT, stats_server, "chat::stats");
    SUP_ADD_CHILD(sup, RES_PERMANENT, acceptor, "chat::acceptor", 2, listener, idle_ms);
    while (WHEREIS("chat::stats") == INVALID_PID)
        YIELD();
    return ntohs(address.sin_port);
}
