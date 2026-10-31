// The load test: simulated users on real sockets, one process each, and a
// coordinator that checks every delivery and the server's counters.
#include "chat.h"
#include <api/message.h>
#include <api/memory.h>
#include <api/group.h>
#include <api/timer.h>
#include <api/io.h>
#include <utils/time.h>
#include <arpa/inet.h>
#include <errno.h>
#include <netinet/in.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

#define READ_TIMEOUT_MS  15000
#define FLOOD_LINES      4000 // Enough to overflow a client that never reads
#define FLOOD_TEXT_BYTES 400
#define LURKER_RCVBUF    4096 // A tiny receive window, so that unread lines pile up on the server

typedef enum { TALKER, TWIN, LURKER, FLOODER, SILENT, CRASHER } Role;

typedef struct {
    Role role;
    int index;
    bool ok;
    long received;
} Result;

/*
    A client's connection
*/

typedef struct {
    int fd;
    char buffer[2 * CHAT_LINE_MAX];
    size_t length;
} Link;

static Link *connect_link(Process *self, int port, int receive_buffer) {
    Link *link = ALLOC(sizeof *link);
    int fd = socket(AF_INET, SOCK_STREAM | SOCK_NONBLOCK | SOCK_CLOEXEC, 0);
    if (!link || fd < 0) {
        FREE(link);
        return NULL;
    }
    if (receive_buffer > 0)
        setsockopt(fd, SOL_SOCKET, SO_RCVBUF, &receive_buffer, sizeof receive_buffer);
    struct sockaddr_in address = {
        .sin_family = AF_INET,
        .sin_port = htons((uint16_t)port),
        .sin_addr.s_addr = htonl(INADDR_LOOPBACK),
    };
    // Non-blocking connect: wait for it as for any other I/O
    int connected = connect(fd, (struct sockaddr *)&address, sizeof address);
    int error = 0;
    socklen_t size = sizeof error;
    if ((connected != 0 && errno != EINPROGRESS) || IO_WAIT_TIMEOUT(fd, IO_WRITE, READ_TIMEOUT_MS) <= 0 ||
        getsockopt(fd, SOL_SOCKET, SO_ERROR, &error, &size) != 0 || error != 0) {
        close(fd);
        FREE(link);
        return NULL;
    }
    *link = (Link){.fd = fd};
    return link;
}

static void close_link(Process *self, Link *link) {
    close(link->fd);
    FREE(link);
}

static bool send_line(Process *self, Link *link, const char *format, ...) {
    char line[CHAT_LINE_MAX];
    va_list arguments;
    va_start(arguments, format);
    int length = vsnprintf(line, sizeof line, format, arguments);
    va_end(arguments);
    if (length < 0 || (size_t)length >= sizeof line)
        return false;

    for (size_t sent = 0; sent < (size_t)length;) {
        ssize_t written = write(link->fd, line + sent, (size_t)length - sent);
        if (written > 0)
            sent += (size_t)written;
        else if ((errno != EAGAIN && errno != EWOULDBLOCK && errno != EINTR) ||
                 IO_WAIT_TIMEOUT(link->fd, IO_WRITE, READ_TIMEOUT_MS) <= 0)
            return false;
    }
    return true;
}

// Reads one line into out, without its newline: 1, 0 at the end of the stream, -1 on error or timeout
static int read_line(Process *self, Link *link, char *out, size_t size, uint64_t timeout_ms) {
    uint64_t deadline = now_ms() + timeout_ms;
    while (1) {
        char *newline = memchr(link->buffer, '\n', link->length);
        if (newline) {
            size_t length = (size_t)(newline - link->buffer);
            if (length >= size)
                return -1;
            memcpy(out, link->buffer, length);
            out[length] = '\0';
            link->length -= length + 1;
            memmove(link->buffer, newline + 1, link->length);
            return 1;
        }
        if (link->length == sizeof link->buffer)
            return -1;

        ssize_t got = read(link->fd, link->buffer + link->length, sizeof link->buffer - link->length);
        if (got > 0) {
            link->length += (size_t)got;
            continue;
        }
        if (got == 0)
            return 0;
        uint64_t now = now_ms();
        if ((errno != EAGAIN && errno != EWOULDBLOCK && errno != EINTR) || now >= deadline ||
            IO_WAIT_TIMEOUT(link->fd, IO_READ, deadline - now) <= 0)
            return -1;
    }
}

static bool expect(Process *self, Link *link, const char *wanted) {
    char line[CHAT_LINE_MAX];
    return read_line(self, link, line, sizeof line, READ_TIMEOUT_MS) == 1 && strcmp(line, wanted) == 0;
}

static void report(Process *self, Pid boss, Result result) {
    SEND(boss, &result, sizeof result, MAKE_ATOM_TAG("load::result"));
}

// Joins the start line, says it is ready, and waits for the start
static void wait_start(Process *self, Pid boss) {
    GROUP_JOIN("load::clients");
    SEND(boss, NULL, 0, MAKE_ATOM_TAG("load::ready"));
    MSG_FREE(RECEIVE(MAKE_ATOM_TAG("load::go")));
}

/*
    The simulated users
*/

// Reads "FROM nick room <from> <seq>"
static bool parse_line(const char *line, int *from, int *seq) {
    if (strncmp(line, "FROM ", 5) != 0)
        return false;
    const char *numbers = strchr(line + 5, ' ');         // After the nickname
    numbers = numbers ? strchr(numbers + 1, ' ') : NULL; // After the room
    if (!numbers)
        return false;
    char *end = NULL;
    long first = strtol(numbers + 1, &end, 10);
    if (end == numbers + 1 || *end != ' ')
        return false;
    char *last = NULL;
    long second = strtol(end + 1, &last, 10);
    if (last == end + 1 || *last != '\0')
        return false;
    *from = (int)first;
    *seq = (int)second;
    return true;
}

static int room_size(int clients, int rooms, int room) {
    return clients / rooms + (room < clients % rooms);
}

typedef struct {
    Pid boss;
    int port, index, clients, rooms, messages;
} TalkerArgs;

// Sends its messages to its room, and checks it gets every member's, once, in order
TASK_WITH(talker, TalkerArgs, args) {
    Pid boss = args->boss;
    int port = args->port, index = args->index, clients = args->clients, rooms = args->rooms, messages = args->messages;
    Result result = {.role = TALKER, .index = index};
    int room = index % rooms;
    Link *link = connect_link(self, port, 0);
    int *last = ALLOC((size_t)clients * sizeof(int));
    if (!link || !last || !send_line(self, link, "NICK user%d\n", index) || !expect(self, link, "OK") ||
        !send_line(self, link, "JOIN room%d\n", room) || !expect(self, link, "OK")) {
        report(self, boss, result);
        return;
    }
    memset(last, 0, (size_t)clients * sizeof(int));
    wait_start(self, boss);

    result.ok = true;
    for (int seq = 1; seq <= messages && result.ok; ++seq)
        result.ok = send_line(self, link, "MSG room%d %d %d\n", room, index, seq);

    long expected = (long)room_size(clients, rooms, room) * messages;
    char line[CHAT_LINE_MAX];
    while (result.ok && result.received < expected) {
        int from = -1, seq = -1;
        result.ok = read_line(self, link, line, sizeof line, READ_TIMEOUT_MS) == 1 && parse_line(line, &from, &seq) &&
                    from >= 0 && from < clients && seq == last[from] + 1;
        if (result.ok)
            last[from] = seq;
        ++result.received;
    }
    result.ok = result.ok && send_line(self, link, "QUIT\n") && expect(self, link, "BYE");
    close_link(self, link);
    report(self, boss, result);
}

// Two of them ask for the same nickname at the same start signal, and both
// keep their connection until both have their answer: one gets it
TASK(twin, Pid boss, int port, int index) {
    Result result = {.role = TWIN, .index = index};
    Link *link = connect_link(self, port, 0);
    wait_start(self, boss);
    char line[CHAT_LINE_MAX];
    if (link && send_line(self, link, "NICK twin\n") &&
        read_line(self, link, line, sizeof line, READ_TIMEOUT_MS) == 1) {
        result.ok = strcmp(line, "OK") == 0;
        result.received = strcmp(line, "ERR taken") == 0 ? 1 : 0;
    }
    report(self, boss, result);
    MSG_FREE(RECEIVE(MAKE_ATOM_TAG("load::release")));
    if (link) {
        send_line(self, link, "QUIT\n");
        close_link(self, link);
    }
}

// Sends a lot to the flood room, reading back each of its lines
TASK(flooder, Pid boss, int port) {
    Result result = {.role = FLOODER};
    Link *link = connect_link(self, port, 0);
    if (!link || !send_line(self, link, "NICK flooder\n") || !expect(self, link, "OK") ||
        !send_line(self, link, "JOIN flood\n") || !expect(self, link, "OK")) {
        report(self, boss, result);
        return;
    }
    wait_start(self, boss);

    char text[FLOOD_TEXT_BYTES + 1];
    memset(text, 'x', FLOOD_TEXT_BYTES);
    text[FLOOD_TEXT_BYTES] = '\0';
    char line[CHAT_LINE_MAX];
    result.ok = true;
    for (int i = 0; i < FLOOD_LINES && result.ok; ++i) {
        result.ok = send_line(self, link, "MSG flood %s\n", text) &&
                    read_line(self, link, line, sizeof line, READ_TIMEOUT_MS) == 1 &&
                    strncmp(line, "FROM flooder", 12) == 0;
        result.received += result.ok;
    }
    send_line(self, link, "QUIT\n");
    close_link(self, link);
    report(self, boss, result);
}

// Joins the flood room and never reads: the server must cut it off, not slow down
TASK(lurker, Pid boss, int port) {
    Result result = {.role = LURKER};
    Link *link = connect_link(self, port, LURKER_RCVBUF);
    if (!link || !send_line(self, link, "NICK lurker\n") || !expect(self, link, "OK") ||
        !send_line(self, link, "JOIN flood\n") || !expect(self, link, "OK")) {
        report(self, boss, result);
        return;
    }
    wait_start(self, boss);
    MSG_FREE(RECEIVE(MAKE_ATOM_TAG("load::flooded")));

    // Now read what is left: some lines, then the end of the stream, long before all of them
    char line[CHAT_LINE_MAX];
    int got;
    while ((got = read_line(self, link, line, sizeof line, READ_TIMEOUT_MS)) == 1)
        ++result.received;
    result.ok = got == 0 && result.received < FLOOD_LINES;
    close_link(self, link);
    report(self, boss, result);
}

// Says nothing: the server must close the connection after its idle timeout
TASK(silent, Pid boss, int port, int idle_ms) {
    Result result = {.role = SILENT};
    // Timed from before the connection: the server's clock starts when it accepts it
    uint64_t start = now_ms();
    Link *link = connect_link(self, port, 0);
    char line[CHAT_LINE_MAX];
    if (link) {
        result.ok = read_line(self, link, line, sizeof line, (uint64_t)idle_ms * 3) == 1 &&
                    strcmp(line, "ERR idle") == 0 && now_ms() - start >= (uint64_t)idle_ms &&
                    read_line(self, link, line, sizeof line, READ_TIMEOUT_MS) == 0;
        close_link(self, link);
    }
    report(self, boss, result);
}

// Makes its own connection crash: the server goes on, and closes its socket
TASK(crasher, Pid boss, int port) {
    Result result = {.role = CRASHER};
    Link *link = connect_link(self, port, 0);
    if (!link || !send_line(self, link, "NICK crasher\n") || !expect(self, link, "OK") ||
        !send_line(self, link, "JOIN room0\n") || !expect(self, link, "OK")) {
        report(self, boss, result);
        return;
    }
    wait_start(self, boss);
    char line[CHAT_LINE_MAX];
    int got = 0;
    if (send_line(self, link, "CRASH\n"))
        while ((got = read_line(self, link, line, sizeof line, READ_TIMEOUT_MS)) == 1)
            ; // room0's lines, until the socket closes
    result.ok = got == 0;
    close_link(self, link);
    report(self, boss, result);
}

/*
    The coordinator
*/

typedef struct {
    int talkers_ok, twins_ok, twins_refused;
    bool flooder_ok, lurker_ok, silent_ok, crasher_ok;
    long received;
} Tally;

typedef struct {
    Pid lurker;
    Pid twins[2];
    int twins_answered;
} Team;

static void spawn_all(Process *self, int port, int clients, int rooms, int messages, int idle_ms, Team *team) {
    for (int i = 0; i < clients; ++i) {
        TalkerArgs args = {self->pid, port, i, clients, rooms, messages};
        SPAWN_WITH(talker, &args, sizeof args);
        REDUCE(50);
    }
    team->twins[0] = SPAWN(twin, 3, self->pid, port, 0);
    team->twins[1] = SPAWN(twin, 3, self->pid, port, 1);
    SPAWN(flooder, 2, self->pid, port);
    team->lurker = SPAWN(lurker, 2, self->pid, port);
    SPAWN(silent, 3, self->pid, port, idle_ms);
    SPAWN(crasher, 2, self->pid, port);
}

static void tally(Process *self, Team *team, Tally *t, const Result *r) {
    switch (r->role) {
        case TALKER:
            t->talkers_ok += r->ok;
            t->received += r->received;
            break;
        case TWIN:
            t->twins_ok += r->ok;
            t->twins_refused += r->received == 1;
            if (++team->twins_answered == 2) // Both asked while the other held its connection
                for (int k = 0; k < 2; ++k)
                    SEND(team->twins[k], NULL, 0, MAKE_ATOM_TAG("load::release"));
            break;
        case FLOODER:
            t->flooder_ok = r->ok;
            SEND(team->lurker, NULL, 0, MAKE_ATOM_TAG("load::flooded"));
            break;
        case LURKER: t->lurker_ok = r->ok; break;
        case SILENT: t->silent_ok = r->ok; break;
        case CRASHER: t->crasher_ok = r->ok; break;
    }
}

static bool check(Process *self, const char *what, bool ok) {
    PRINT("  [%s] %s\n", ok ? " ok " : "FAIL", what);
    return ok;
}

bool chat_selftest(Process *self, int port, int clients, int rooms, int messages, int idle_ms) {
    Team team = {0};
    PRINT("chat selftest: %d users in %d rooms, %d messages each, on port %d\n", clients, rooms, messages, port);
    spawn_all(self, port, clients, rooms, messages, idle_ms, &team);

    int ready = clients + 5; // Everyone but the silent client waits for the start
    for (int i = 0; i < ready; ++i)
        MSG_FREE(RECEIVE(MAKE_ATOM_TAG("load::ready")));
    uint64_t start = now_ms();
    GROUP_SEND("load::clients", NULL, 0, MAKE_ATOM_TAG("load::go"));

    Tally t = {0};
    uint64_t talkers_done = start;
    for (int pending = clients + 6; pending > 0; --pending) {
        Message *msg = RECEIVE(MAKE_ATOM_TAG("load::result"));
        const Result *r = msg->payload;
        tally(self, &team, &t, r);
        if (r->role == TALKER)
            talkers_done = now_ms();
        MSG_FREE(msg);
    }

    long expected = 0;
    for (int room = 0; room < rooms; ++room)
        expected += (long)room_size(clients, rooms, room) * room_size(clients, rooms, room) * messages;
    ChatStats stats = {0};
    bool have_stats = chat_stats(self, &stats);
    double seconds = (double)(talkers_done > start ? talkers_done - start : 1) / 1000.0;

    bool ok = check(self, "every talker got every line of its room, once, in order",
                    t.talkers_ok == clients && t.received == expected);
    ok &= check(self, "one twin got the nickname, the other was refused", t.twins_ok == 1 && t.twins_refused == 1);
    ok &= check(self, "the flooder got its own lines back", t.flooder_ok);
    ok &= check(self, "the client that never reads was cut off", t.lurker_ok && stats.slow >= 1);
    ok &= check(self, "the silent client was closed after the idle timeout", t.silent_ok && stats.idle >= 1);
    ok &= check(self, "a crashed connection was closed, the others went on", t.crasher_ok && stats.crashed >= 1);
    ok &= check(self, "the server counted every delivery",
                have_stats && stats.deliveries >= expected + FLOOD_LINES && stats.connections == clients + 6);
    PRINT("  %ld lines delivered to talkers in %.2f s: %.0f lines/s\n", t.received, seconds,
          (double)t.received / seconds);
    PRINT("  server: %ld connections, %ld broadcasts, %ld deliveries, %ld dropped, %ld slow, %ld idle, %ld crashed\n",
          stats.connections, stats.broadcasts, stats.deliveries, stats.dropped, stats.slow, stats.idle, stats.crashed);
    PRINT("%s\n", ok ? "selftest passed" : "selftest FAILED");
    return ok;
}
