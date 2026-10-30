# Monitors, links and failures

Headers: `api/monitor.h`, `api/link.h`

CLPF gives you three tools to react to processes ending: **monitors** (be
told), **links** (fail together) and **kills** (end someone). Supervisors are
built from them.

## Monitors: being told

```c
MONITOR(pid);     // from now on, a DOWN message arrives when pid ends
DEMONITOR(pid);   // stop; a DOWN already received stays in the mailbox
```

The DOWN message has the tag `MAKE_DOWN_TAG(pid)` and a `DownInfo` payload:

```c
typedef struct {
    Pid from;            // the process that ended
    unsigned int reason; // its exit reason
} DownInfo;
```

Wait for a given process with `RECEIVE(MAKE_DOWN_TAG(pid))`, or for any of
them with `RECEIVE_DOWN()`. DOWN messages are ordinary messages: they wake a
process blocked in `RECEIVE`, and a server can handle them in its main loop
(`msg->tag.kind == TAG_KIND_DOWN`).

Monitoring a process that is already gone is not an error: the DOWN arrives
at once, with `EXIT_REASON_NOPROC`. So you never miss an exit, even when
the process ends right after you got its pid.

```c
#include <api/task.h>
#include <api/message.h>
#include <api/monitor.h>
#include <api/io.h>

TASK0(short_job) {
    PRINT("working\n");
}

TASK(entry, int argc, char *argv[]) {
    Pid job = SPAWN_OPTS(short_job, .monitor = true); // monitored before it runs
    Message *msg = RECEIVE(MAKE_DOWN_TAG(job));
    DownInfo *down = msg->payload;
    PRINT("job %ld ended, reason %u\n", down->from, down->reason); // 0: normal
    MSG_FREE(msg);
}

CLPF_ENTRY(entry)
```

Monitors are one-way, and you can stack them: a process can monitor many
others, be monitored by many, and monitoring never affects the watched
process.

## Links: failing together

```c
LINK(pid);     // 0, or -1 if pid does not exist or is self
UNLINK(pid);   // 0, or -1 if they were not linked
Pid child = SPAWN_LINK(func, argc, args...); // linked before the child runs
```

A link is bidirectional. When one side ends:

- **normally**: nothing happens to the other side;
- **abnormally** (error, killed, shutdown...): an *exit signal* with the same
  reason goes to the other side, which ends with that reason, unless it traps
  exits.

Use links for processes that are useless without each other, such as a
connection handler and its parser. A link propagates along a chain: if A is
linked to B and B to C, a crash of C takes B down, which takes A down.

## Trapping exits

`TRAP_EXIT(true)` turns incoming exit signals into **EXIT messages** instead
of ending the process. The tag is `MAKE_EXIT_TAG(from)` and the payload an
`ExitInfo { from, reason }`; `RECEIVE_EXIT()` waits for any of them.

```c
#include <api/task.h>
#include <api/message.h>
#include <api/link.h>
#include <api/io.h>

TASK0(fragile) {
    EXIT_ERROR();
}

TASK(entry, int argc, char *argv[]) {
    TRAP_EXIT(true);
    Pid child = SPAWN_LINK(fragile, 0);

    Message *msg = RECEIVE_EXIT(); // instead of dying with the child
    ExitInfo *info = msg->payload;
    PRINT("%ld exited (reason %u), I am still here\n", info->from, info->reason);
    MSG_FREE(msg);
    (void)child;
}

CLPF_ENTRY(entry)
```

Supervisors trap exits: they are linked to their children, so that a dying
supervisor takes its children with it, and they must survive their children's
crashes.

## Killing and exit signals

```c
KILL(pid);               // ends pid with EXIT_REASON_KILLED, cannot be trapped
SEND_EXIT(pid, reason);  // an exit signal, as a link would send it
```

`KILL` returns `0`, or `-1` if `pid` does not exist or is already ending.
`KILL(self->pid)` ends the caller immediately.

`SEND_EXIT(pid, reason)` follows the link rules:

| Target | `EXIT_REASON_NORMAL` | Other reasons |
|---|---|---|
| Not trapping exits | Ignored | Ends with `reason` |
| Trapping exits | EXIT message | EXIT message |

A kill is not instantaneous: the target is marked, and its scheduler ends it
the next time it would run it, without resuming its code. A process stuck in a
loop that never calls the runtime cannot be killed until it does.

## Choosing

| You want to... | Use |
|---|---|
| Know when a process ends, and why | `MONITOR` |
| Make two processes live and die together | `LINK` / `SPAWN_LINK` |
| Handle a child's crash yourself | `SPAWN_LINK` + `TRAP_EXIT(true)`, or `MONITOR` |
| Restart children by policy | A [supervisor](supervision.md) |
| Stop a process now | `KILL` |
| Ask a process to stop and let it clean up | `SEND_EXIT(pid, EXIT_REASON_SHUTDOWN)` to a process trapping exits |
