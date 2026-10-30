# Patterns

Recipes for structuring programs with processes. Each complete example
compiles and runs as is.

## A server process

A process that owns some state and serves requests, like Erlang's
`gen_server`. The state lives in local variables of the loop. Nobody else can
touch it, so no lock is needed.

```c
#include <api/task.h>
#include <api/message.h>
#include <api/registry.h>
#include <api/io.h>
#include <stdio.h>
#include <string.h>

typedef struct {
    char key[16];
    int value;
} Entry;

// A key/value store: "put" stores, "get" answers, "stop" ends the server
TASK0(kv_store) {
    Entry entries[64];
    int count = 0;

    while (1) {
        Message *msg = RECEIVE(NO_TAG);
        Entry *request = msg->payload;

        if (MSG_IS(msg, MAKE_STR_TAG("put")) && count < 64) {
            entries[count++] = *request; // "cast": no reply
        } else if (MSG_IS(msg, MAKE_STR_TAG("get"))) {
            int value = -1;
            for (int i = 0; i < count; ++i)
                if (strcmp(entries[i].key, request->key) == 0)
                    value = entries[i].value;
            SEND(msg->sender_pid, &value, sizeof value, msg->tag); // "call": reply
        } else if (MSG_IS(msg, MAKE_STR_TAG("stop"))) {
            MSG_FREE(msg);
            EXIT_NORMAL();
        }
        MSG_FREE(msg);
    }
}

// A small client API hides the messages from callers
static void DEF(kv_put, const char *key, int value) {
    Entry entry = {"", value};
    snprintf(entry.key, sizeof entry.key, "%s", key);
    SEND(WHEREIS("kv"), &entry, sizeof entry, MAKE_STR_TAG("put"));
}

static int DEF(kv_get, const char *key) {
    Entry entry = {"", 0};
    snprintf(entry.key, sizeof entry.key, "%s", key);
    Message *reply = REQUEST(WHEREIS("kv"), &entry, sizeof entry, MAKE_STR_TAG("get"));
    int value = reply ? *(int *)reply->payload : -1;
    MSG_FREE(reply);
    return value;
}

TASK(entry, int argc, char *argv[]) {
    REGISTER("kv", SPAWN0(kv_store));
    CALL(kv_put, "answer", 42);
    CALL(kv_put, "year", 2025);
    PRINT("answer = %d, year = %d\n", CALL(kv_get, "answer"), CALL(kv_get, "year"));
}

CLPF_ENTRY(entry)
```

The same shape covers caches, connection managers and resource pools. Put
the server under a supervisor and register it on start, and clients keep
finding it by name across restarts.

## A supervised worker pool

Workers ask for jobs, so a slow job never blocks the others, and a crashing
job only costs its own worker, which the supervisor replaces.

```c
#include <api/task.h>
#include <api/message.h>
#include <api/supervisor.h>
#include <api/io.h>

#define WORKERS 3
#define JOBS    10

TASK(worker, Pid dispatcher) {
    while (1) {
        SEND(dispatcher, NULL, 0, MAKE_STR_TAG("ready"));
        Message *msg = RECEIVE(NO_TAG);
        if (MSG_IS(msg, MAKE_STR_TAG("stop"))) {
            MSG_FREE(msg);
            EXIT_NORMAL();
        }
        int job = *(int *)msg->payload;
        MSG_FREE(msg);
        if (job == 7)
            EXIT_ERROR(); // a bad job: the supervisor starts a new worker
        int result = job * job;
        SEND(dispatcher, &result, sizeof result, MAKE_STR_TAG("result"));
    }
}

TASK(entry, int argc, char *argv[]) {
    Pid pool = SPAWN_SUPERVISOR("pool", SUP_ONE_FOR_ONE, 1000, 5);
    for (int i = 0; i < WORKERS; ++i)
        SUP_ADD_CHILD(pool, RES_TRANSIENT, worker, "worker", 1, self->pid);

    int next = 0, results = 0, stopped = 0;
    while (stopped < WORKERS) {
        Message *msg = RECEIVE(NO_TAG);
        if (MSG_IS(msg, MAKE_STR_TAG("ready"))) {
            if (next < JOBS) {
                SEND(msg->sender_pid, &next, sizeof next, MAKE_STR_TAG("job"));
                ++next;
            } else {
                SEND(msg->sender_pid, NULL, 0, MAKE_STR_TAG("stop"));
                ++stopped;
            }
        } else if (MSG_IS(msg, MAKE_STR_TAG("result"))) {
            ++results;
        }
        MSG_FREE(msg);
    }
    PRINT("%d results, 1 job lost to a crash\n", results);
}

CLPF_ENTRY(entry)
```

`RES_TRANSIENT` means a worker that stops normally is not restarted, while a
crashed one is. A real dispatcher would also [monitor](guides/failures.md)
the workers, to resend the job a crashed worker was holding.

## A pipeline

Each stage is a process that transforms what it receives and passes it on.
Stages run in parallel on different cores, and each mailbox buffers the
difference in speed between neighbours.

```c
#include <api/task.h>
#include <api/message.h>
#include <api/io.h>

// Applies `factor`, then forwards to `next` (the last stage reports to the sink)
TASK(stage, int factor, Pid next) {
    while (1) {
        Message *msg = RECEIVE(NO_TAG);
        int value = *(int *)msg->payload * factor;
        bool last = MSG_IS(msg, MAKE_STR_TAG("last"));
        SEND(next, &value, sizeof value, msg->tag);
        MSG_FREE(msg);
        if (last)
            EXIT_NORMAL();
    }
}

TASK(entry, int argc, char *argv[]) {
    // Built from the end: entry <- times 10 <- times 2 <- source
    Pid times10 = SPAWN(stage, 2, 10, self->pid);
    Pid times2 = SPAWN(stage, 2, 2, times10);

    for (int i = 1; i <= 5; ++i)
        SEND(times2, &i, sizeof i, i == 5 ? MAKE_STR_TAG("last") : MAKE_STR_TAG("item"));

    Message *msg;
    do {
        msg = RECEIVE(NO_TAG);
        PRINT("%d\n", *(int *)msg->payload); // 20 40 60 80 100, in order
        bool last = MSG_IS(msg, MAKE_STR_TAG("last"));
        MSG_FREE(msg);
        if (last)
            break;
    } while (1);
}

CLPF_ENTRY(entry)
```

Order is preserved, since each stage is a single process and messages between
two processes stay in order. To parallelise a slow stage, put a pool behind a
dispatcher, at the cost of ordering.

## Calling a server that may die

`REQUEST` waits for ever. When the server may crash, monitor it and wait for
whichever comes first, the reply or its DOWN, with a custom matcher:

```c
#include <api/task.h>
#include <api/message.h>
#include <api/monitor.h>
#include <api/io.h>

TASK0(dying_server) {
    MSG_FREE(RECEIVE(NO_TAG));
    EXIT_ERROR(); // crashes instead of answering
}

// Accepts the reply tag, or the DOWN of the server (pid in the pattern's int_value)
static bool reply_or_down(const Message *msg, Tag pattern) {
    return MSG_IS(msg, MAKE_STR_TAG("reply")) ||
           (msg->tag.kind == TAG_KIND_DOWN && msg->tag.int_value == pattern.int_value);
}

TASK(entry, int argc, char *argv[]) {
    Pid server = SPAWN0(dying_server);
    MONITOR(server);
    SEND(server, NULL, 0, MAKE_STR_TAG("reply"));

    Message *msg = RECEIVE_IF(reply_or_down, MAKE_INT_TAG(server));
    if (msg->tag.kind == TAG_KIND_DOWN)
        PRINT("the server died before answering\n");
    MSG_FREE(msg);
    DEMONITOR(server);
}

CLPF_ENTRY(entry)
```

Add a deadline with `RECEIVE_IF_TIMEOUT` when the server may also hang.

## Cleaning up before stopping

A process that holds an external resource, such as a file or a socket, can
trap exits to close it when its supervisor stops it. A `SHUTDOWN` exit signal
then arrives as a message:

```c
TASK(owner_of_a_file, const char *path) {
    TRAP_EXIT(true);
    FILE *file = fopen(path, "a");
    while (1) {
        Message *msg = RECEIVE(NO_TAG);
        if (msg->tag.kind == TAG_KIND_EXIT) { // stop requested, or a linked process died
            MSG_FREE(msg);
            if (file)
                fclose(file);
            EXIT_NORMAL();
        }
        /* ... write what you receive ... */
        MSG_FREE(msg);
    }
}
```

Supervisors wait up to `SUP_SHUTDOWN_TIMEOUT_MS` (5 s) before killing it.

## Moving from threads

| Threaded C | CLPF |
|---|---|
| `pthread_create` | `SPAWN` |
| A struct protected by a mutex | A server process owning the struct |
| `pthread_cond_wait` on a queue | `RECEIVE` |
| A work queue + thread pool | Dispatcher + supervised workers |
| `pthread_join` | `MONITOR` + receive the DOWN |
| A thread crash kills the program | The process ends; its supervisor restarts it |
| Global configuration | A registered server, or read-only `malloc` memory |
| Blocking `read`/`accept` in a thread | A non-blocking fd and `IO_WAIT` in a process per connection |

The biggest change is the design, not the code: give each piece of state
exactly one owner process, and make everything else ask it.
