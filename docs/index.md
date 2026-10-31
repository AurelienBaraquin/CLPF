# CLPF

**CLPF (Concurrent Lightweight Process Framework) brings the Erlang model of
concurrency to C.** You write your program as many small *processes* that
share nothing and talk only through *messages*. *Supervisors* restart the
processes that fail. The runtime spreads the processes over all your cores.

```c
#include <api/task.h>
#include <api/message.h>
#include <api/io.h>

TASK(greeter, Pid reply_to) {
    SEND(reply_to, "hello", 6, MAKE_STR_TAG("greeting"));
}

TASK(entry, int argc, char *argv[]) {
    SPAWN(greeter, 1, self->pid);
    Message *msg = RECEIVE(MAKE_STR_TAG("greeting"));
    PRINT("got: %s\n", (char *)msg->payload);
    MSG_FREE(msg);
}

CLPF_ENTRY(entry)
```

## Why it exists

Writing concurrent C usually means threads, shared memory and locks. Those
three together give you data races, deadlocks and crashes that take the whole
program down. The Erlang world solved this decades ago with a different model,
and CLPF makes that model available in plain C:

- **Processes, not threads.** A process is a coroutine with its own stack,
  heap and mailbox. Creating one costs a few microseconds and a few kilobytes,
  so you can have tens of thousands of them. They are scheduled M:N on a pool
  of OS threads.
- **Messages, not shared memory.** A process never touches another process's
  data. It sends a copy, which the receiver picks out of its mailbox, by tag,
  when it is ready. There is no lock in user code.
- **Let it crash.** A process that hits an unexpected state just ends.
  Monitors, links and supervisors notice it and restart it in a known good
  state. Recovery code lives in one place, not in every function.

It suits programs made of many independent activities: network servers,
pipelines, job systems, simulations, device or connection managers. In those
programs a failure should stay local, and the code should read like a
sequence of steps instead of a tangle of callbacks.

## What you get

| Area | Features |
|---|---|
| Processes | `SPAWN` with arguments or a copied data block, priorities, configurable stacks with overflow reports, heap limits, `HIBERNATE` |
| Messages | `SEND`, selective `RECEIVE` by tag or matcher, `REQUEST_REF`/`REPLY` with unique references, notifications, timeouts, mailbox limits |
| Failures | `MONITOR` (DOWN messages), `LINK`, `TRAP_EXIT`, `KILL` |
| Shared data | Process groups (`GROUP_SEND`), shared binaries sent without a copy (`SEND_BIN`), shared key-value tables (`TABLE_*`) |
| Supervision | `ONE_FOR_ONE`, `ONE_FOR_ALL`, `REST_FOR_ONE`, permanent/transient/temporary children, restart intensity |
| Services | Name registry, one-shot and periodic timers, ordered output (`PRINT`), non-blocking stdin (`GETLINE`), sockets and pipes (`IO_WAIT`, `IO_WATCH`), leveled logs |
| Observability | `PROCESS_INFO`, `PROCESS_LIST`, a dump on `SIGUSR1`, a watchdog for processes that never yield, [benchmarks](benchmarks.md) |
| Runtime | Work-stealing schedulers, reduction-based preemption, sleeping when idle, clean shutdown with an exit code |
| Quality | Tested in Debug and Release, and under AddressSanitizer and ThreadSanitizer, in CI |

## Where to go next

- **[Getting started](getting-started.md):** install the library and write your first program.
- **[Concepts](concepts.md):** the model, and why it fits C.
- **[Guides](guides/processes.md):** each feature in depth, with examples.
- **[Patterns](patterns.md):** servers, worker pools, pipelines, and moving from threads.
- **[API reference](api.md):** every macro and type.
- **[Architecture](architecture.md):** how the runtime works inside.
- **[Benchmarks](benchmarks.md):** what processes and messages cost.
- **[Troubleshooting and FAQ](troubleshooting.md):** the usual pitfalls.

!!! warning "Status"
    CLPF is experimental. It runs on **x86-64 Linux** only, and a process is
    only preempted when it calls the runtime. See the
    [FAQ](troubleshooting.md#what-are-the-limits) for the full list of limits.
