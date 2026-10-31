# Observability

Header: `api/introspection.h`

A system made of thousands of processes needs a way to see what each one is
doing. CLPF gives three tools: snapshots of processes, a dump of all of
them, and a watchdog for processes that never give the CPU back. For log
lines, see [logging](io.md#logging).

## Looking at processes

| Function | Meaning |
|---|---|
| `PROCESS_INFO(pid, &info)` | Fills a `ProcessInfo`: `0`, or `-1` if `pid` is not alive |
| `PROCESS_LIST(buffer, max)` | Writes up to `max` live pids to `buffer`; returns how many processes are alive (may be more than `max`) |
| `clpf_dump(FILE *out)` | Writes a table of every live process to `out` |
| `clpf_state_name(state)`, `clpf_priority_name(priority)` | Names for printing |

All of them work from any thread, also outside processes. A `ProcessInfo` is
a snapshot taken under the process's lock:

| Field | Meaning |
|---|---|
| `pid`, `name`, `registered_name` | Identity: the label given at spawn, and the registry name (empty if none) |
| `state` | `PROCESS_RUNNING`, `PROCESS_READY`, `PROCESS_WAITING` (in a `RECEIVE`), `PROCESS_INIT` (never ran yet) or `PROCESS_EXITING` |
| `priority`, `scheduler` | Its priority, and the index of the scheduler it belongs to |
| `queued` | Messages waiting in its mailbox |
| `heap_bytes` | Memory reserved by its heap |
| `runs` | Times it was resumed |
| `links`, `children`, `monitored_by` | Bidirectional links, live children, and processes that get a DOWN when it ends |
| `trap_exit`, `system` | Whether it traps exits, and whether it is a runtime process |

```c
#include <api/task.h>
#include <api/message.h>
#include <api/registry.h>
#include <api/timer.h>
#include <api/io.h>
#include <api/introspection.h>

TASK0(worker) {
    MSG_FREE(RECEIVE(MAKE_STR_TAG("stop")));
}

TASK(entry, int argc, char *argv[]) {
    Pid pid = SPAWN0_NAMED(worker, "worker");
    REGISTER("app::worker", pid);
    SEND(pid, NULL, 0, MAKE_STR_TAG("job")); // it waits for "stop": this one stays queued
    SLEEP(10);

    ProcessInfo info;
    if (PROCESS_INFO(pid, &info) == 0)
        PRINT("%s is %s, %zu message(s) queued\n", info.name, clpf_state_name(info.state), info.queued);

    Pid pids[64];
    PRINT("%zu processes alive\n", PROCESS_LIST(pids, 64));
    SEND(pid, NULL, 0, MAKE_STR_TAG("stop"));
}

CLPF_ENTRY(entry)
```

A growing `queued` is the usual sign of an overloaded process: it receives
faster than it handles. Give it a [mailbox limit](messages.md#backpressure),
or spread the work.

## Dumping everything

`clpf_dump` writes one line per process:

```text
PID        STATE    PRIORITY  SCHED  QUEUED       HEAP       RUNS  NAME (REGISTERED)
2          waiting  medium        1       0      16384          1  server::supervisor
3          running  high          2       1      16384          2  server::io (server::io)
4          running  medium        3       0      16384          2  clpf::entry
5          waiting  medium        0       1      16384          2  worker (app::worker)
4 processes
```

Processes are listed by slot. Pid 1 was the launcher, which starts the
system processes and the entry, then ends.

A running program writes the same table to stderr when it receives
`SIGUSR1`, so you can look at a live system without changing it:

```bash
kill -USR1 $(pidof my_program)
```

The runtime blocks `SIGUSR1` in its threads and waits for it in a dedicated
thread, so the dump never runs inside a signal handler. Threads you create
from the entry process inherit that mask. If your program needs `SIGUSR1`
for itself, it cannot use it with CLPF.

## The watchdog

Scheduling is cooperative: a process that loops without calling the runtime
keeps its scheduler, and every process queued behind it waits (see
[scheduling](scheduling.md)). The watchdog reports such a process once,
on stderr:

```text
clpf: watchdog: process 'parser' (pid 42) has run for 1000 ms on scheduler 3 without yielding; call REDUCE in long loops
```

The limit is 1000 ms by default. Set it with the `CLPF_WATCHDOG_MS`
environment variable; `0` disables the watchdog:

```bash
CLPF_WATCHDOG_MS=200 ./my_program
```

The watchdog only reports: it never stops the process. It costs a clock
read each time a process is resumed. The main thread checks twice per
limit while processes run, and not at all while everything waits, so an
idle program still uses no CPU.
