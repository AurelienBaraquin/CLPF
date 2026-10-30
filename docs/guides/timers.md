# Timers

Header: `api/timer.h`

| Macro | Meaning |
|---|---|
| `SEND_AFTER(pid, ms, data, size, tag)` | Sends a copy of the message to `pid` after `ms`, as if the caller sent it. Returns a `TimerId` (never 0), or 0 if out of memory |
| `CANCEL_TIMER(id)` | `0`, or `-1` if the timer already fired or was cancelled |
| `RECEIVE_TIMEOUT(tag, ms)` | `RECEIVE`, giving up with `NULL` after `ms` |
| `RECEIVE_IF_TIMEOUT(matcher, pattern, ms)` | The same with a matcher |
| `SLEEP(ms)` | Waits `ms` without blocking the scheduler |

Timers are one-shot. For a periodic tick, send yourself the next timer each
time you handle one.

```c
#include <api/task.h>
#include <api/message.h>
#include <api/timer.h>
#include <api/io.h>

TASK(entry, int argc, char *argv[]) {
    // A periodic tick: each tick schedules the next one
    SEND_AFTER(self->pid, 10, NULL, 0, MAKE_STR_TAG("tick"));
    for (int ticks = 1; ticks <= 3; ++ticks) {
        MSG_FREE(RECEIVE(MAKE_STR_TAG("tick")));
        PRINT("tick %d\n", ticks);
        SEND_AFTER(self->pid, 10, NULL, 0, MAKE_STR_TAG("tick"));
    }

    // Waiting with a deadline
    Message *reply = RECEIVE_TIMEOUT(MAKE_STR_TAG("reply"), 50);
    if (!reply)
        PRINT("no reply within 50 ms\n");
    MSG_FREE(reply);
}

CLPF_ENTRY(entry)
```

## Guarantees

- A timer fires **no earlier** than its delay. It fires late only when the
  machine is overloaded.
- `CANCEL_TIMER` returning `0` means the message will never arrive. If it
  returns `-1` for a timer you did not cancel, the message has already been
  delivered: it is in the mailbox.
- `RECEIVE_TIMEOUT` never leaves anything behind: when a message arrives in
  time, its internal timeout is cancelled, and removed from the mailbox if it
  fired at the same moment.
- `RECEIVE_TIMEOUT(tag, 0)` only polls, like `FETCH`.
- Messages that arrive during a `SLEEP` stay in the mailbox.

## How they work

The main thread keeps all timers in a min-heap and sleeps until the next
deadline, so pending timers cost nothing while they wait. Adding a timer
wakes it so that it can sleep for less.
