# Architecture

How the runtime works inside. You don't need this page to use CLPF, but it
explains its costs and guarantees. Source paths are relative to the
repository root.

## Overview

```text
                 main thread                      scheduler threads (1 per core - 2)
   +-------------------------------+        +--------------------------------------+
   | destroys exited processes     |        | run queues (very high, high, medium) |
   | fires timers (min-heap)       | <----> | wakeup queue    shared run queues    |
   | drives the shutdown           |  MPSC  | runs processes (libaco coroutines)   |
   +-------------------------------+        +--------------------------------------+
            ^                                    ^              |
            | core messages                      | messages     | steal requests
   +--------+-----------+                 +------+--------------v-------+
   | stdin reader thread |  ---------->   | process table (65 536 slots) |
   | epoll poller thread |   messages     +------------------------------+
   +---------------------+
```

## Processes and pids

`include/process.h`, `src/process.c`, `src/clpf_api/task.c`

All processes live in one table of 65 536 slots, allocated at startup as
zeroed memory and left untouched until used: a zeroed slot is a free one
(`pid == PID_FREE`, 0), and with glibc a zeroed mutex is a valid one. Slot 0
is never used, so no valid pid is 0. Slots are handed out in order, then
reused from a stack of released ones, and scans of the table stop at the
last slot ever used. An idle program is about 2 MB resident.

A **pid** is `index | generation << 16`. The generation of a slot increases
each time it is reused, so the pid of a dead process never designates a
newer one.

Each slot has a **lock** that guards everything other threads can touch: the
pid itself, the mailbox inbox, the watcher and link lists, the trap-exit flag
and pending kills. `process_lock(pid)` finds a slot, locks it and checks
that it still holds that pid. Senders hold that lock while they deliver, and
the main thread holds it while it marks a slot free. A message therefore
either reaches a live process or fails cleanly.

Spawning runs on the caller's thread: it takes a free slot under the table
lock, initialises the process under its slot lock, and pushes it to the least
loaded scheduler. The child cannot run before `SPAWN` returns its pid.

## Coroutines

`src/scheduler_loop.c`, `src/utils/funcall.S`, `external/libaco`

A process is a [libaco](https://github.com/hnes/libaco) coroutine with its
own stack. libaco puts a guard page below the stack, and a `SIGSEGV` handler
recognises a fault on it to report the overflowing process by name
(`src/utils/stack_guard.c`). Switching between processes is a register swap,
with no system call. Stacks of the default size are reused: an ended process
gives its stack to a bounded pool (`src/memory/stack.c`,
`src/memory/block_pool.c`), and the mapping, guard page included, stays as is.

The task's arguments are passed by `funcall.S`, a small x86-64 trampoline
that loads up to six machine words into the System V argument registers
(`self` first), with the stack 16-byte aligned. That trampoline and libaco's
switch are the two pieces of x86-64 assembly that tie CLPF to the
architecture.

## Schedulers

`src/scheduler.c`, `src/scheduler_loop.c`

Each scheduler thread owns:

- three **private run queues** (very high, high, medium/low), with a counter
  each, touched only by its own thread;
- three **shared run queues** where other threads push new or migrating
  processes;
- a **wakeup queue** of processes that received something.

All cross-thread queues are **intrusive lock-free MPSC queues**: the node
lives in the queued element, a push is one CAS, and the consumer takes
everything with one atomic exchange, then reverses the list to get FIFO order
(`src/utils/mpscq.c`). Pushing never allocates.

A pick drains the wakeup and shared queues, serves pending steal requests,
then takes the first ready process by priority. Low priority processes are
skipped 7 times out of 8, unless nothing else is ready.

### Waiting and waking

A waiting process is in **no queue at all**. A delivery to it, made under its
slot lock, sets its `wake_pending` flag. If the flag was clear, the delivery
pushes the process once onto its scheduler's wakeup queue and wakes that
scheduler. The scheduler only revisits processes that actually received
something, so a blocked process costs nothing however many there are.
Senders never write another process's state, so no wakeup can be lost.

Before it waits, a `RECEIVE` publishes under its slot lock what it accepts:
its matcher and pattern, unless messages arrived since it last looked, in
which case it looks again instead of waiting. A sender only wakes it for a
message that matcher accepts, or one from the runtime, and the waker clears
the filter. Each `RECEIVE` keeps a cursor on the last message it examined,
so a wakeup only scans what arrived since; `REQUEST_REF` starts its cursor
at the newest message older than the request.

### Idle

With nothing to run, a scheduler spins briefly, then sleeps on an
`IdleWaiter` (mutex, condition variable and two atomics, `src/utils/idle.c`),
until its next timer at most. The sleeper announces that it sleeps, then
checks for work again; a waker publishes work, then signals. Whichever comes
second sees the other.

A scheduler with at least two ready processes wakes **one** sleeping
scheduler to share them, and none while an idle one is still awake and
looking for work: that one will ask.

### Work stealing

An idle scheduler sets its bit in the `steal_requests` mask of the scheduler
with the most ready processes, and asks again every few milliseconds while
some scheduler has work to spare. At its next pick, the busy scheduler shares
its ready processes evenly with the requesters, least urgent first. To migrate
a process, it changes the process's scheduler under its slot lock, points its
coroutine back to the new thread's main context, and pushes it to the new
scheduler's shared queue. A process with a wakeup still queued on its old
scheduler is not moved.

### Preemption

Each process has a reduction budget. Runtime calls spend it, and the process
yields when it runs out (`clpf_reduce`). A pending kill is checked before a
process is resumed: a killed process is ended by its scheduler without
running again.

## Mailboxes and memory

`src/mailbox.c`, `src/memory/safe_heap.c`

Every process has a TLSF heap on memory mapped for it, which grows on demand
and is unmapped at once when the process ends, except its initial pool, which
goes to a bounded pool for the next process. A spinlock guards it, since
senders on other threads allocate their messages in it.

A message is **one allocation in the receiver's heap**: the header, the
16-byte aligned payload, then the tag string. Atom tags are not copied:
`src/atom.c` interns names in a hash map (`src/utils/hash_map.c`) that lives
as long as the program, and `MAKE_ATOM_TAG` caches its atom in a static at
each call site. The mailbox has two lists:

- the **inbox**, appended to by senders under the receiver's slot lock;
- the **seen list**, private to the owner. A receive moves the inbox to the
  end of the seen list, then scans it for the first match.

## Monitors, links and exits

`src/clpf_api/base.c`, `src/clpf_api/link.c`, `src/clpf_api/monitor.c`

A process keeps the list of its watchers and of its links, both under its
slot lock. When it ends, `process_terminate` detaches both lists and marks the
process as exiting, under the lock. Without holding any lock, it then sends
a DOWN message to each watcher and an exit signal to each linked process.
An exit signal delivered to a process that traps exits becomes an EXIT
message; otherwise it becomes a pending kill.

Monitoring or linking a process that is already exiting fails cleanly: the
monitor gets an immediate `NOPROC` DOWN, the link returns `-1`. Two slot
locks are only ever taken in table order, so links cannot deadlock.

## The main thread

`src/clpf.c`, `src/memory/reaper.c`

The main thread is mostly asleep. It:

- unmaps memory for the schedulers: the stacks and heaps of ended processes
  that the pools cannot keep go to a lock-free queue, the **reaper**, so
  that the threads running processes never pay for a `munmap` and the TLB
  flush of every core it causes. A batch wakes the main thread once;
- runs the watchdog: each scheduler publishes when it resumed its current
  process (`src/watchdog.c`). While processes run, the main thread checks
  twice per limit. When nothing has run for a while it sleeps without a
  deadline, and asks the next resume to wake it. Both sides use sequentially
  consistent atomics, so a process that starts as the main thread goes to
  sleep is never missed;
- drives the shutdown.

Wakeups of the main thread go through `clpf_wake_main`, which counts them:
the main thread notes the count before it looks at everything, and only
sleeps if it did not change, so no wakeup is lost.

A dedicated thread waits for `SIGUSR1` with `sigwait` and writes
`clpf_dump` (`src/introspection.c`); the runtime blocks the signal in all
its threads, and joins that thread before freeing the process table.

## Ending a process

`src/process_end.c`

The scheduler that ran a process ends it, once its coroutine is done and
its pending wakeup taken out of the queue: it releases its registered name,
leaves its parent's children, drops its groups and tables, destroys it
and frees its slot. Its fd watches are dropped earlier, before its DOWN
messages are sent, so that a monitor that owns its sockets may close them
at once. Only then does it count the process out,
so the main thread never frees the runtime under a process being destroyed.

Each scheduler keeps up to 64 free slots and trades them with the table by
batches of 32, and the user process count is atomic: spawning and ending a
process take no global lock, unless it had a registered name.

## Timers

`src/timer.c`

Each scheduler owns a `TimerQueue`: a min-heap of timers, and a table of
slots that `TimerId`s point to. An id is `queue << 56 | generation << 32 |
slot`: cancelling finds the timer in O(1) and takes it out of the heap in
O(log n), and the generation makes a spent id fail. Timers go to the queue
of the scheduler whose thread arms them. The scheduler fires due timers at
the top of its loop, publishes its next deadline in an atomic, and sleeps
until it at most. A timer armed from another thread wakes the queue's
scheduler, which checks that deadline against its own before sleeping.

Timers fire under their queue's lock, so a failed `CANCEL_TIMER` means the
message is already in the mailbox, and `RECEIVE_TIMEOUT` can always clean up
its own timeout message.

## System processes

`src/native_task/`

At startup the runtime spawns a **launcher**. The launcher starts the
**system supervisor**, which keeps the **I/O server** (`server::io`) running,
then spawns the entry process. `PRINT` sends formatted text to the I/O server,
which writes it with `write()`, retrying short writes. Log lines take the
same path to stderr.

`GETLINE` requests go to a dedicated **stdin thread** (`src/stdin_reader.c`),
which blocks in `getline()` and answers with a message. Supervisors are
ordinary processes (`src/native_task/supervisor/`).

`IO_WAIT` and `IO_WATCH` register the fd with a dedicated **epoll thread**
(`src/io_poller.c`), one-shot. A table indexed by fd records the watcher
and a sequence number, which travels in the epoll event so that an event
from a cancelled or replaced watch is ignored. When an fd fires, the thread
removes it from epoll and sends `MAKE_FD_TAG(fd)` to the watcher. A process's
watches are dropped as it ends, before its monitors hear of it.

## Hibernation

`HIBERNATE` stores the next function and its arguments as the process's task
call, flags it, and ends its coroutine with `aco_exit`. Back on the
scheduler, the flag makes it destroy the coroutine, give the stack back,
trim the heap, and park the process: waiting, with no wait filter, unless a
message or a kill is already there. Its next resume finds no coroutine and
creates one on the new task call, as for a first run. Trimming removes the
pools that hold no allocation (`tlsf_remove_pool`), and hands the pages
inside the free blocks of the first pool back with `MADV_DONTNEED`.

## Preemption holds

A sender runs the receiver's `RECEIVE_IF` matcher under the receiver's lock
(see waiting and waking). With `clpf::auto_reduce`, that matcher's own
function calls would spend the sender's reductions and could make it yield
while holding the lock. The runtime counts such regions per thread
(`clpf_preemption_hold`), and the hooks only reduce outside them.

## Groups, binaries and tables

`src/clpf_api/group.c`, `src/bin.c`, `src/clpf_api/bin.c`, `src/clpf_api/table.c`

Names are looked up in a string hash map (`src/utils/hash_map.c`): the
registry, groups and tables use it.

**Groups** are arrays of pids under one lock, found by name in the map. `GROUP_SEND` copies the members
and sends outside the lock, since sending takes the receivers' locks.

A **binary** is a `malloc` block with a header: a magic number, an atomic
reference count and the size. Each process keeps, under its slot lock, a
list of the references it holds. A binary message is allocated in the
receiver's heap as the header, the list node, then the tag string; its
payload points into the binary. Delivery takes the reference and links the
node; `MSG_FREE` unlinks it and releases the reference. `process_destroy`
releases the whole list before the heap goes, which covers queued messages
and received ones never freed.

A **table** is a chained hash map behind a `pthread_rwlock_t`. A directory
of slots, under its own lock, maps a `TableId` (`generation << 32 | index + 1`)
to the table. An operation takes the table lock before it releases the
directory lock, and a deletion first removes the table from the directory,
then takes the write lock: it waits for the operations in progress, and no
new one can start.

The scheduler that ends a process drops its group memberships and tables
before destroying it; per-process counters skip the work for processes that
had none.

## Shutdown

When the entry process ends, its scheduler wakes the main thread, which:

1. records its exit reason;
2. kills every non-system process, sweeping again if dying processes spawned
   new ones;
3. sends a `SHUTDOWN` exit signal to the system supervisor. It stops the I/O
   server gracefully, and the server writes everything queued first, since
   mailboxes are FIFO;
4. once no process is left, stops and joins the schedulers, frees the
   runtime, and returns 0 or 1.

## Testing the runtime

The `tests/` directory checks each behaviour above with a small CLPF program:
lost wakeups, pid reuse, stack alignment, starvation, stealing, shutdown
output, and so on. The CI runs the suite in Debug and Release, and under
AddressSanitizer and ThreadSanitizer.

ThreadSanitizer keeps a shadow call stack per thread, which coroutines that
move between threads would unbalance. Under it, each process is a
ThreadSanitizer fiber (`include/utils/fiber.h`): the scheduler switches to
it right before resuming the process, and the process switches back right
before giving the CPU away. libaco, which only switches stacks, is built
without the instrumentation, so that these switches line up with the stack
switches.
