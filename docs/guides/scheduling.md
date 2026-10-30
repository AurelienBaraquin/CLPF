# Scheduling and performance

## Schedulers

The runtime starts one scheduler thread per usable core, minus two left to
the main thread and the system, and at least one. The count honours CPU
affinity (`taskset`, containers). Each scheduler runs its processes one at a
time; different schedulers run in parallel.

New processes go to the least loaded scheduler. When a scheduler runs out of
work, it asks the busiest one, which hands over half of its ready processes
the next time it picks one. Processes therefore move between threads:
never keep thread-local state, or a thread id, in a process.

A scheduler with nothing to do spins for a few microseconds, so that quick
replies don't pay for a wakeup, then sleeps until work arrives. An idle
program uses no CPU.

## When a process gives the CPU back

A process runs until it:

- waits: `RECEIVE`, `REQUEST`, `RECEIVE_TIMEOUT`, `SLEEP`, `GETLINE`;
- calls `YIELD()`;
- uses up its **reduction budget** (2000). `SEND` costs 10, `FETCH` costs
  1, and `REDUCE(n)` costs `n`. When the budget runs out the process yields,
  and every yield refills it.

The runtime cannot interrupt C code that never calls it. A long computation
must call `REDUCE`, or `YIELD`, regularly, otherwise it keeps its scheduler,
and every process queued behind it waits:

```c
#include <api/task.h>
#include <api/message.h>
#include <api/io.h>

TASK(sum_squares, long n, Pid report_to) {
    long long sum = 0;
    for (long i = 0; i < n; ++i) {
        sum += (long long)i * i;
        REDUCE(1); // about one reduction per loop iteration
    }
    SEND(report_to, &sum, sizeof sum, MAKE_STR_TAG("sum"));
}

TASK(entry, int argc, char *argv[]) {
    SPAWN(sum_squares, 2, 1000000L, self->pid);
    // Wait for the result: the program ends when the entry process does
    Message *msg = RECEIVE(MAKE_STR_TAG("sum"));
    PRINT("sum = %lld\n", *(long long *)msg->payload);
    MSG_FREE(msg);
}

CLPF_ENTRY(entry)
```

`REDUCE` only costs a decrement and a test until the budget runs out, so it
is fine in hot loops. Scale `n` to the work done: `REDUCE(10)` for an
iteration that does ten times more.

## Priorities

`PROCESS_PRIORITY_VERY_HIGH` and `HIGH` processes run before medium and low
ones on their scheduler. A `LOW` process gets one turn for every 8 of a
`MEDIUM` one. See [processes](processes.md#priorities).

## Performance notes

Measured on the test machine (6 cores, 4 schedulers, Release build):

| Scenario | Result |
|---|---|
| 5000 request/reply round trips between two processes | ~12 ms |
| 20 000 round trips across schedulers | ~45 ms |
| 5000 round trips next to 10 000 blocked processes | ~20 ms (waiting processes cost nothing) |
| Idle runtime | ~20 µs of CPU per 500 ms |

Tips:

- Spawning is cheap, but each process maps its own stack and heap: around
  32 KiB of address space at least. Tens of thousands of processes are fine;
  the table holds 65 536 at once.
- Messages are copied: send small payloads, or batch many small ones into one
  struct.
- Receive what arrives. A mailbox full of messages nobody matches makes every
  receive slower.
- Blocking system calls block a whole scheduler: see [I/O](io.md#other-io).
