# CLPF API reference

Every macro below is used inside a task body: it relies on the implicit
`Process *self` that `TASK` gives the function. Functions that are not tasks
can receive `self` as their first parameter (`DEF` / `CALL` do that for you).

Headers live under `include/api/`; once installed, they are under
`include/clpf/api/` and the include directory is `include/clpf`.

- [Tasks and lifetime](#tasks-and-lifetime)
- [Spawning](#spawning)
- [Messages](#messages)
- [Tags and matchers](#tags-and-matchers)
- [Monitors](#monitors)
- [Links and exit signals](#links-and-exit-signals)
- [Registry](#registry)
- [Timers](#timers)
- [Supervisors](#supervisors)
- [Memory and I/O](#memory-and-io)
- [Scheduling](#scheduling)
- [Outside a process](#outside-a-process)
- [Constants](#constants)
- [Build options and limits](#build-options-and-limits)

Each section is a summary; the [guides](guides/processes.md) explain the
behaviour in depth.

| Header | Contents |
|---|---|
| `api/base.h` | `TASK`, `EXIT_*`, `YIELD`, `REDUCE`, `CLPF_ENTRY`, exit reasons |
| `api/task.h` | `SPAWN*` (includes `base.h` and `message.h`) |
| `api/message.h` | `SEND`, `RECEIVE*`, `FETCH*`, `REQUEST`, matchers, `NOTIFY` |
| `api/monitor.h` | `MONITOR`, `DEMONITOR`, `DownInfo` |
| `api/link.h` | `LINK`, `TRAP_EXIT`, `KILL`, `SEND_EXIT`, `SPAWN_LINK`, `ExitInfo` |
| `api/registry.h` | `REGISTER`, `WHEREIS`, `UNREGISTER` |
| `api/timer.h` | `SEND_AFTER`, `CANCEL_TIMER`, `RECEIVE_TIMEOUT`, `SLEEP` |
| `api/supervisor.h` | `SPAWN_SUPERVISOR`, `SUP_ADD_CHILD*`, `SUP_REMOVE_CHILD` |
| `api/memory.h` | `ALLOC`, `FREE` |
| `api/io.h` | `PRINT`, `FORMAT`, `GETLINE`, `IO_WAIT*`, `IO_WATCH`, `IO_UNWATCH` |
| `api/group.h` | `GROUP_JOIN`, `GROUP_LEAVE`, `GROUP_MEMBERS`, `GROUP_SEND` |
| `api/bin.h` | `BIN_ALLOC`, `SEND_BIN`, `BIN_FREE`, `BIN_SIZE`, `BIN_REFS` |
| `api/table.h` | `TABLE_NEW`, `TABLE_FIND`, `TABLE_PUT`, `TABLE_GET`, `TABLE_DELETE`, `TABLE_COUNT`, `TABLE_DROP` |
| `api/introspection.h` | `PROCESS_INFO`, `PROCESS_LIST`, `clpf_dump`, `ProcessInfo` |
| `api/log.h` | `LOG_DEBUG`, `LOG_INFO`, `LOG_WARN`, `LOG_ERROR`, `clpf_set_log_level` |

## Tasks and lifetime

`api/base.h`

| Macro | Meaning |
|---|---|
| `TASK0(name)` | Task without arguments: `void name(Process *self)` |
| `TASK(name, args...)` | Task with up to 5 integer or pointer arguments |
| `TASK_WITH(name, Type, var)` | Task receiving a copied argument block as `Type *var` (see `SPAWN_WITH`) |
| `DEF(name, args...)` / `CALL(name, args...)` | Declare / call a helper function that gets `self` |
| `CLPF_ENTRY(task)` | Generates `main()`; `task` gets `(int argc, char *argv[])` |
| `EXIT_NORMAL()`, `EXIT_ERROR()`, `EXIT(reason)` | End the calling process at once; returning from the task is `EXIT_NORMAL()` |
| `PARENT()`, `CHILD(i)`, `NB_CHILDREN()` | Spawner of the process, and its live children |
| `HIBERNATE(func, argc, args...)`, `HIBERNATE0(func)` | Drop the stack, give back unused memory, and start again in `func` with these arguments at the next message; never return |

Exit reasons (`exit_reason`): `EXIT_REASON_NORMAL`, `EXIT_REASON_ERROR`,
`EXIT_REASON_NOPROC` (monitored process already gone), `EXIT_REASON_KILLED`
(`KILL`), `EXIT_REASON_SHUTDOWN` (stopped by its supervisor or the runtime),
`EXIT_REASON_HEAP_LIMIT` (its heap would have grown past `max_heap_size`).

**The program ends with its entry task.** When the task given to `CLPF_ENTRY`
ends, every other user process is killed, output queued for `PRINT` is
written, and the program exits with 0 if the entry task exited normally, 1
otherwise.

## Spawning

`api/task.h`. Spawning is synchronous and never yields: the pid is known
before the new process can run. New processes go to the least loaded
scheduler. `SPAWN*` return the pid, or `INVALID_PID`.

| Macro | Meaning |
|---|---|
| `SPAWN(func, argc, args...)` | Spawn `func` with `argc` (≤ 5) integer/pointer arguments |
| `SPAWN0(func)` | Spawn a `TASK0` |
| `SPAWN_NAMED(func, name, argc, args...)`, `SPAWN0_NAMED(func, name)` | With a label (logs only, not a registered name) |
| `SPAWN_PRIO(func, prio, argc, args...)`, `SPAWN0_PRIO(func, prio)` | With a priority |
| `SPAWN_PRIO_NAMED(...)`, `SPAWN0_PRIO_NAMED(...)` | Both |
| `SPAWN_WITH(func, &args, sizeof args)` | Copies a block of any data into the new process (for `TASK_WITH`) |
| `SPAWN_LINK(func, argc, args...)` | Spawn and link before the child runs (`api/link.h`) |
| `SPAWN_OPTS(func, .field = value...)` | Spawn a no-argument task with `SpawnOptions` |

`SpawnOptions` (zero-initialised fields take the defaults):

| Field | Default | Meaning |
|---|---|---|
| `name` | `""` | Label |
| `priority` | `PROCESS_PRIORITY_MEDIUM` | Also `_LOW` (runs 1 pass in 8), `_HIGH`, `_VERY_HIGH` |
| `monitor` | `false` | The spawner monitors the child from its very start |
| `link` | `false` | The spawner and the child are linked before it runs |
| `trap_exit` | `false` | Traps exit signals from the start, as if it called `TRAP_EXIT(true)` before running |
| `arg_block`, `arg_block_size` | none | Block copied into the child heap and passed as its argument |
| `stack_size` | `CLPF_DEFAULT_STACK_SIZE` (16 KiB) | Stack of the process |
| `mailbox_limit` | `0` (none) | Messages that may wait; past it `SEND` returns `SEND_FULL` |
| `max_heap_size` | `0` (none) | Bytes its heap may take; past it, the process ends with `EXIT_REASON_HEAP_LIMIT` |
| `scheduler` | `0` | `1 + index` of the scheduler to start on; `0` picks the least loaded |

`clpf_spawn(self, &funcall, options)` spawns with integer arguments and options.

## Messages

`api/message.h`. A message is copied, payload and tag string included, into
the receiver's heap: **pointers inside a payload mean nothing to the receiver**.
Mailboxes are FIFO per sender.

| Macro | Meaning |
|---|---|
| `SEND(pid, data, size, tag)` | Returns 0, -1 if `pid` does not exist (not an error for the runtime), or `SEND_FULL` if its mailbox is full |
| `RECEIVE(tag)` | Blocks until the oldest message with this tag arrives; `NO_TAG` takes any message |
| `RECEIVE_IF(matcher, pattern)` | Blocks until the oldest message accepted by a matcher |
| `FETCH(tag)`, `FETCH_IF(matcher, pattern)` | Same without blocking: `NULL` only if nothing matches |
| `REQUEST(pid, data, size, tag)` | Sends, then waits for the reply carrying the same tag (`NULL` if `pid` is gone) |
| `REQUEST_TIMEOUT(pid, data, size, tag, ms)` | `REQUEST` giving up with `NULL` after `ms` (`CLPF_INFINITY`: never) |
| `REQUEST_REF(pid, data, size, ms)` | Request tagged with a fresh reference; waits for the `REPLY`; late replies are dropped |
| `REPLY(msg, data, size)` | Answers a request received with `REQUEST_REF` or `REQUEST` |
| `MAKE_REF()` | A value unique for the run (never 0); `MAKE_REF_TAG(ref)` makes it a tag |
| `MSG_IS(msg, tag)` | Tag equality test |
| `MSG_FREE(msg)` | Releases a received message (accepts `NULL`) |
| `NOTIFY(pid, name)`, `WAIT_NOTIF(name)` | Empty messages identified by any string |

`Message` fields: `tag`, `payload` (`NULL` when empty, 16-byte aligned),
`size`, `sender_pid` (`INVALID_PID` when sent by the runtime).

## Tags and matchers

`mailbox.h` (included by every API header)

| Constructor | Kind |
|---|---|
| `MAKE_INT_TAG(n)` | Integer |
| `MAKE_ATOM_TAG("s")` | Atom: interned once per name, compared by pointer, never copied; `s` is a literal |
| `clpf_atom_tag(s)`, `clpf_atom(s)` | The atom tag, or the atom, of any string |
| `MAKE_STR_TAG(s)` | String, copied with the message |
| `MAKE_INTERNAL_TAG(code)` | Application-defined code |
| `MAKE_NOTIFY_TAG(s)` | Notification (see `NOTIFY`) |
| `MAKE_DOWN_TAG(pid)` | DOWN of `pid` (see monitors) |
| `MAKE_EXIT_TAG(pid)` | Trapped exit of `pid` (see links) |
| `MAKE_FD_TAG(fd)` | `fd` became ready (see `IO_WATCH`) |
| `MAKE_RANGE_TAG(min, max)` | Pattern for `TM_IN_RANGE` (32-bit bounds) |
| `MAKE_TAG_KIND(kind)` | Pattern for `TM_KIND` |
| `NO_TAG` | Pattern matching any message with `TM_SAME` |

Matchers: `TM_SAME` (equal tags), `TM_PREFIX` (string starting with the
pattern), `TM_GTE` / `TM_LTE` (integer bounds), `TM_IN_RANGE`, `TM_KIND`.
A matcher is any `bool (*)(const Message *msg, Tag pattern)`.

## Monitors

`api/monitor.h`

| Macro | Meaning |
|---|---|
| `MONITOR(pid)` | A DOWN message will arrive when `pid` exits (right away, `EXIT_REASON_NOPROC`, if it is gone) |
| `DEMONITOR(pid)` | Stops it; a DOWN already received stays in the mailbox |
| `RECEIVE_DOWN()` | Waits for the DOWN of any monitored process |

DOWN messages are tagged `MAKE_DOWN_TAG(pid)` and carry a `DownInfo { from, reason }`.

## Links and exit signals

`api/link.h`

| Macro | Meaning |
|---|---|
| `LINK(pid)`, `UNLINK(pid)` | Bidirectional link; 0 or -1 |
| `SPAWN_LINK(func, argc, args...)` | Spawn and link in one step |
| `TRAP_EXIT(true/false)` | Exit signals (except `KILL`) arrive as EXIT messages instead of ending the process |
| `KILL(pid)` | Ends `pid` with `EXIT_REASON_KILLED`, cannot be trapped; `pid` may be `self->pid` |
| `SEND_EXIT(pid, reason)` | Exit signal as a link would send it |
| `RECEIVE_EXIT()` | Waits for a trapped exit |

When a linked process ends with a reason other than normal, its partners end
with the same reason, or get an EXIT message (`MAKE_EXIT_TAG(from)`,
`ExitInfo { from, reason }`) if they trap exits. A normal exit is not
propagated. A kill takes effect the next time the target's scheduler would
run it.

## Registry

`api/registry.h`. Names use `[A-Za-z0-9_:]`; a process has at most one name,
released when it exits.

| Macro | Meaning |
|---|---|
| `REGISTER(name, pid)` | 0, or -1 if the name is invalid or taken, or `pid` is gone or already named |
| `UNREGISTER(name)` | 0, or -1 if not registered |
| `WHEREIS(name)` | The pid, or `INVALID_PID` |

The I/O server is registered as `server::io`.

## Timers

`api/timer.h`

| Macro | Meaning |
|---|---|
| `SEND_AFTER(pid, ms, data, size, tag)` | Sends a copy of the message after `ms`, as if the caller sent it; returns a `TimerId` |
| `SEND_INTERVAL(pid, ms, data, size, tag)` | Sends a copy every `ms` until `CANCEL_TIMER` or until `pid` is gone |
| `CANCEL_TIMER(id)` | 0, or -1 if it already fired or was cancelled |
| `RECEIVE_TIMEOUT(tag, ms)`, `RECEIVE_IF_TIMEOUT(matcher, pattern, ms)` | `RECEIVE` returning `NULL` after `ms` (0 only polls) |
| `SLEEP(ms)` | Waits without blocking the scheduler |

## Groups and shared data

See [groups and shared data](guides/shared-data.md).

| Macro | Meaning |
|---|---|
| `GROUP_JOIN(name)`, `GROUP_LEAVE(name)` | `0` or `-1`; a process leaves its groups when it ends |
| `GROUP_MEMBERS(name, buffer, max)` | Up to `max` members in join order; returns how many there are |
| `GROUP_SEND(name, data, size, tag)` | A copy to every member; returns how many got it |
| `BIN_ALLOC(size)` | A shared binary, or `NULL`; the caller holds a reference |
| `SEND_BIN(pid, data, tag)` | Sends the binary without a copy; `MSG_FREE` releases the receiver's reference |
| `BIN_FREE(data)` | Releases the caller's `BIN_ALLOC` reference: `0` or `-1` |
| `BIN_SIZE(data)`, `BIN_REFS(data)` | Size and current references |
| `TABLE_NEW(name)`, `TABLE_FIND(name)` | A `TableId`, or `0` |
| `TABLE_PUT(id, key, data, size)`, `TABLE_DELETE(id, key)` | `0` or `-1` |
| `TABLE_GET(id, key, size_out)` | A copy in the caller's heap (`FREE` it), or `NULL` |
| `TABLE_COUNT(id)` | Keys, or `-1` |
| `TABLE_DROP(id)` | Owner only: `0` or `-1`; a table is also deleted with its owner |

## Supervisors

`api/supervisor.h`

| Macro | Meaning |
|---|---|
| `SPAWN_SUPERVISOR(name, strategy, period_ms, max_restarts)` | 0 for period or max picks 1000 ms / 5 |
| `SUP_ADD_CHILD(sup, restart, func, name, argc, args...)`, `SUP_ADD_CHILD0(...)` | Starts a child; returns its pid or `INVALID_PID` |
| `SUP_ADD_CHILD_PRIO(...)`, `SUP_ADD_CHILD_PRIO0(...)` | With a priority |
| `SUP_ADD_CHILD_OPTS(sup, restart, func, .field = value...)` | With `SpawnOptions`: name, priority, stack, mailbox limit, argument block (kept for restarts) |
| `SUP_REMOVE_CHILD(sup, pid)` | Stops supervising it (it keeps running); 0 or -1 |

Strategies: `SUP_ONE_FOR_ONE` (restart the child that died), `SUP_ONE_FOR_ALL`
(stop all, restart all), `SUP_REST_FOR_ONE` (stop and restart it and the
children started after it). Restart types: `RES_PERMANENT` (always),
`RES_TRANSIENT` (unless it exited normally), `RES_TEMPORARY` (never).

Children are monitored and linked. A supervisor stops a child with a
`SHUTDOWN` exit signal and kills it if it is still alive after
`SUP_SHUTDOWN_TIMEOUT_MS` (5 s), so a child trapping exits can clean up. More
than `max_restarts` restarts within `period_ms`, all children together, and
the supervisor stops its children and exits with `EXIT_REASON_ERROR`.

## Memory and I/O

`api/memory.h`, `api/io.h`, `api/log.h`

| Macro | Meaning |
|---|---|
| `ALLOC(size)`, `FREE(ptr)` | Allocation in the process heap, released anyway when the process exits |
| `PRINT(fmt, ...)` | Formatted output through the I/O server: lines from different processes never interleave |
| `FORMAT(fmt, ...)` | Formatted string allocated with `ALLOC` (`FREE` it) |
| `GETLINE()` | A line of stdin with its newline (`FREE` it), or `NULL` at the end of input; blocks only the caller |
| `IO_WAIT(fd, events)` | Waits until `fd` is ready for `IO_READ` and/or `IO_WRITE`; the ready flags (may include `IO_ERROR`, `IO_HANGUP`), or `-1`; blocks only the caller |
| `IO_WAIT_TIMEOUT(fd, events, ms)` | The same, `0` after `ms` |
| `IO_WATCH(fd, events)` | One message `MAKE_FD_TAG(fd)` with an `int` of ready flags when `fd` is ready; `0` or `-1` (bad fd, regular file, watched by another process) |
| `IO_UNWATCH(fd)` | `0`, or `-1` if no watch is pending |
| `LOG_DEBUG(fmt, ...)` ... `LOG_ERROR` | One line on stderr, `time LEVEL name<pid>: message`, through the I/O server; dropped below the current level |
| `clpf_set_log_level(level)`, `clpf_log_level()` | `LOG_LEVEL_DEBUG` ... `LOG_LEVEL_NONE`; starts from `CLPF_LOG_LEVEL`, `info` by default |

## Scheduling

| Macro | Meaning |
|---|---|
| `YIELD()` | Lets the other ready processes of the scheduler run |
| `REDUCE(n)` | Spends `n` of the process's reduction budget (2000); the process yields when it is used up |
| `clpf::auto_reduce` (CMake target) | Builds the code that links it with `-finstrument-functions`: each of its function calls spends a reduction |

Scheduling is cooperative: a process gives the CPU away in runtime calls
(`SEND`, `FETCH`, `RECEIVE` when it waits, `YIELD`, `REDUCE`...). A long
computation should call `REDUCE` regularly. Idle schedulers take ready
processes from busy ones.

## Outside a process

These functions take no `self` and are safe from any thread, including
threads you create yourself:

| Function | Meaning |
|---|---|
| `int clpf_main(Task entry, int argc, char *argv[])` | Runs the runtime with `entry` as entry process; returns the exit code. `CLPF_ENTRY` calls it |
| `int clpf_send(NULL, Pid pid, void *data, size_t size, Tag tag)` | Sends a message from outside any process; the receiver sees `sender_pid == INVALID_PID` |
| `Pid clpf_whereis(const char *name)` | `WHEREIS` |

A thread of your own that blocks on I/O can feed a process this way.

## Introspection

`api/introspection.h`, usable from any thread

| Function | Meaning |
|---|---|
| `PROCESS_INFO(pid, &info)` | A snapshot of a process in a `ProcessInfo` (state, priority, scheduler, `queued`, `heap_bytes`, `runs`, `links`, `children`, `monitored_by`, names...): `0`, or `-1` if it is not alive |
| `PROCESS_LIST(buffer, max)` | Up to `max` live pids in `buffer`; returns how many are alive |
| `clpf_dump(FILE *out)` | A table of every live process; also written to stderr on `SIGUSR1` |
| `clpf_state_name(state)`, `clpf_priority_name(priority)` | Names for printing |

## Environment variables

| Variable | Meaning |
|---|---|
| `CLPF_LOG_LEVEL` | `debug`, `info` (default), `warn`, `error` or `none` |
| `CLPF_WATCHDOG_MS` | Report a process running this long without yielding (default 1000, `0` disables) |
| `CLPF_POOL_PREWARM` | Map this many stacks and heaps at startup (up to the pools' capacity, 1024), so that the first processes spawn without mapping memory |

## Constants

| Constant | Value | Meaning |
|---|---|---|
| `INVALID_PID` | `-1` | No process |
| `SEND_FULL` | `-2` | `SEND` result when the receiver's mailbox is full |
| `CLPF_INFINITY` | `UINT64_MAX` | A timeout that never expires |
| `MAX_PROCESSES` | 65 536 | Process table slots; 65 535 live processes at once (slot 0 is never used) |
| `MAX_SCHEDULERS` | 64 | Scheduler threads at most |
| `NAME_MAX_LENGTH` | 64 | Labels and registered names, terminating NUL included |
| `CLPF_DEFAULT_STACK_SIZE` | 16 KiB | Default process stack (build-time option) |
| `REDUCTION_BUDGET` | 2000 | Reductions between two forced yields |
| `REDUCTIONS_SEND`, `REDUCTIONS_FETCH` | 10, 1 | Cost of `SEND` and `FETCH` |
| `LOW_PRIORITY_SKIPS` | 7 | A low priority process runs one pass in 8 |
| `SUP_SHUTDOWN_TIMEOUT_MS` | 5000 | Time a child trapping exits gets to stop |

## Build options and limits

CMake options: `CMAKE_BUILD_TYPE` (default `Debug`), `CLPF_SANITIZE=address|thread`,
`CLPF_TRACE=ON` (log process creation and destruction), `CLPF_BUILD_TESTS`,
`CLPF_BUILD_EXAMPLES`, `CLPF_BUILD_BENCH` (target `bench`, see [benchmarks](benchmarks.md)), and `-DCLPF_DEFAULT_STACK_SIZE=...` in `CMAKE_C_FLAGS`.

Using the installed library: `find_package(clpf)` and link `clpf::clpf`, or
`pkg-config --cflags --libs clpf`. The API needs GCC or Clang: the macros
use GNU C statement expressions (`-std=gnu11` recommended).

Limits: x86-64 Linux only (libaco's context switch and `funcall.S` are
x86-64 assembly); up to 65 535 live processes; a process name is at most 63
characters; preemption only happens in runtime calls.
