# Supervision

Header: `api/supervisor.h`

A supervisor is a process that starts children, watches them and restarts
them when they fail, following a strategy. It turns "let it crash" into a
working system.

## Starting a supervisor

```c
Pid sup = SPAWN_SUPERVISOR(name, strategy, period_ms, max_restarts);
```

- `strategy`: what to restart when a child fails (see below).
- `period_ms`, `max_restarts`: the **restart intensity**. If more than
  `max_restarts` restarts happen within `period_ms`, for all children
  together, the supervisor gives up (see below). `0` picks the defaults:
  1000 ms and 5 restarts.

## Adding children

```c
Pid pid = SUP_ADD_CHILD(sup, restart_type, func, "name", argc, args...);
Pid pid = SUP_ADD_CHILD0(sup, restart_type, func, "name");
Pid pid = SUP_ADD_CHILD_PRIO(sup, restart_type, func, "name", priority, argc, args...);
int ok  = SUP_REMOVE_CHILD(sup, pid);   // 0, or -1 if it is not one of sup's children
```

`SUP_ADD_CHILD` starts the child right away and returns its pid, or
`INVALID_PID` if it could not start. The child is restarted with the **same
function and arguments**. Since it starts from scratch, any state it needs
must come from its arguments or be fetched again, for instance from a
registered process.

`SUP_REMOVE_CHILD` stops supervising the child; the child keeps running.

### Restart types

| Type | Restarted when it ends... |
|---|---|
| `RES_PERMANENT` | always, even after a normal exit: a service that must always run |
| `RES_TRANSIENT` | only after an abnormal exit: a job that must finish successfully |
| `RES_TEMPORARY` | never: a one-shot job; the supervisor just forgets it |

## Strategies

| Strategy | When a child fails |
|---|---|
| `SUP_ONE_FOR_ONE` | Only that child is restarted |
| `SUP_ONE_FOR_ALL` | All children are stopped (last started first), then all are started again in order |
| `SUP_REST_FOR_ONE` | The children started **after** it are stopped, then it and they are started again in order |

- `ONE_FOR_ONE` suits independent children: one worker per connection.
- `ONE_FOR_ALL` suits children that only make sense together, such as a
  set sharing a protocol state.
- `REST_FOR_ONE` suits a chain where later children depend on earlier ones:
  a database connection, then a cache built on it, then an API using both.

Stopping a child means sending it an `EXIT_REASON_SHUTDOWN` exit signal and
waiting for its DOWN. A child that does not trap exits ends at once. A child
that traps them gets up to `SUP_SHUTDOWN_TIMEOUT_MS` (5 s) to clean up, then
it is killed. Temporary children stopped this way are not restarted.

## When the supervisor gives up

Restarting forever would hide a persistent bug and burn the CPU. Once the
intensity is exceeded, the supervisor:

1. stops all its children;
2. exits with `EXIT_REASON_ERROR`.

Its monitors and links see that exit. If it is itself supervised, its own
supervisor applies its strategy, so the failure moves up the tree. If
nothing watches it, the failure is visible to whoever started it.

```c
#include <api/task.h>
#include <api/message.h>
#include <api/monitor.h>
#include <api/supervisor.h>
#include <api/io.h>

TASK(always_failing, Pid boss) {
    SEND(boss, NULL, 0, MAKE_STR_TAG("attempt"));
    EXIT_ERROR();
}

TASK(entry, int argc, char *argv[]) {
    Pid sup = SPAWN_SUPERVISOR("fragile", SUP_ONE_FOR_ONE, 1000, 2);
    MONITOR(sup);
    SUP_ADD_CHILD(sup, RES_PERMANENT, always_failing, "worker", 1, self->pid);

    for (int i = 0; i < 3; ++i) // the first start, then 2 restarts
        MSG_FREE(RECEIVE(MAKE_STR_TAG("attempt")));

    Message *down = RECEIVE(MAKE_DOWN_TAG(sup));
    PRINT("supervisor gave up, reason %u\n", ((DownInfo *)down->payload)->reason); // 1: error
    MSG_FREE(down);
}

CLPF_ENTRY(entry)
```

## Supervisors and links

A supervisor monitors and **links** its children, and traps exits:

- a crashing child does not take the supervisor down; its DOWN drives the
  restart;
- a supervisor that is killed, or fails, takes its children with it, so no
  orphan keeps running unsupervised;
- a supervisor stops when its parent sends it an exit signal, or when the
  runtime shuts down.

## Building a tree

A supervisor's children can be supervisors. Since a supervisor is started
with `SPAWN_SUPERVISOR`, the usual way is a small task that builds a subtree
and returns its pid to the level above, or registers it:

```c
#include <api/task.h>
#include <api/message.h>
#include <api/registry.h>
#include <api/supervisor.h>
#include <api/io.h>

TASK0(db_connection) {
    MSG_FREE(RECEIVE(MAKE_STR_TAG("never")));
}

TASK(cache, int size) {
    MSG_FREE(RECEIVE(MAKE_STR_TAG("never")));
}

TASK(entry, int argc, char *argv[]) {
    // The storage layer: the cache depends on the connection
    Pid storage = SPAWN_SUPERVISOR("storage", SUP_REST_FOR_ONE, 5000, 3);
    REGISTER("storage", storage);
    SUP_ADD_CHILD0(storage, RES_PERMANENT, db_connection, "db");
    SUP_ADD_CHILD(storage, RES_PERMANENT, cache, "cache", 1, 1024);

    PRINT("storage layer up: %ld\n", WHEREIS("storage"));
}

CLPF_ENTRY(entry)
```

Design rules that work well:

- Put the processes that are most likely to fail at the **bottom** of the
  tree, and the ones that hold important state near the top.
- Keep supervisor intensities small and periods short at the bottom: a
  burst of crashes should escalate quickly instead of looping.
- A process that needs another one's pid after a restart should look it up
  by name ([registry](registry.md)) instead of keeping an old pid.

## The system supervisor

The runtime runs its own supervisor for its I/O server (`server::io`,
permanent). If the I/O server dies, it is restarted and takes its name back,
and `PRINT` keeps working.
