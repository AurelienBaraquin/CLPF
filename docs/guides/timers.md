# Timers

Header: `api/timer.h`

| Macro | Meaning |
|---|---|
| `SEND_AFTER(pid, ms, data, size, tag)` | Sends a copy of the message to `pid` after `ms`, as if the caller sent it. Returns a `TimerId` (never 0), or 0 if out of memory |
| `SEND_INTERVAL(pid, ms, data, size, tag)` | Sends a copy every `ms` until cancelled or until `pid` is gone. Returns a `TimerId` |
| `CANCEL_TIMER(id)` | `0`, or `-1` if the timer already fired or was cancelled |
| `RECEIVE_TIMEOUT(tag, ms)` | `RECEIVE`, giving up with `NULL` after `ms` |
| `RECEIVE_IF_TIMEOUT(matcher, pattern, ms)` | The same with a matcher |
| `SLEEP(ms)` | Waits `ms` without blocking the scheduler |

`SEND_AFTER` timers fire once; `SEND_INTERVAL` timers fire until they are
cancelled. A periodic timer that falls behind, on an overloaded machine,
skips the ticks it missed instead of firing them in a burst.

```c
#include <api/task.h>
#include <api/message.h>
#include <api/timer.h>
#include <api/io.h>

TASK(entry, int argc, char *argv[]) {
    // A periodic tick, every 10 ms
    TimerId ticker = SEND_INTERVAL(self->pid, 10, NULL, 0, MAKE_STR_TAG("tick"));
    for (int ticks = 1; ticks <= 3; ++ticks) {
        MSG_FREE(RECEIVE(MAKE_STR_TAG("tick")));
        PRINT("tick %d\n", ticks);
    }
    CANCEL_TIMER(ticker);

    // A one-shot timer
    SEND_AFTER(self->pid, 20, NULL, 0, MAKE_STR_TAG("later"));
    MSG_FREE(RECEIVE(MAKE_STR_TAG("later")));

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

Each scheduler keeps the timers armed by the processes it runs in its own
min-heap, fires them in its loop and sleeps until the next one, so pending
timers cost nothing while they wait, and arming one takes no global lock.
A `TimerId` names its queue and slot, so `CANCEL_TIMER` finds a timer at
once however many are pending.

A timer fires on the scheduler that armed it, even if the process moved
since. A scheduler busy running a process that does not yield fires its
timers late: call `REDUCE` in long loops (see [scheduling](scheduling.md)).
