# Concepts

CLPF implements the *actor model* the way Erlang does it. This page explains
the ideas, and why they are worth having in C.

## The problem with threads and locks

In classic concurrent C, threads share memory and protect it with locks:

```c
pthread_mutex_lock(&queue_lock);
push(&queue, job);           // forgetting the lock somewhere is a data race
pthread_cond_signal(&queue_not_empty);
pthread_mutex_unlock(&queue_lock);
```

Every shared structure needs its own discipline, and that discipline is not
checked by anything. Two locks taken in different orders deadlock. A bug in
one thread can corrupt memory every other thread uses. A crash, even in
unrelated code, ends the whole process. Event loops (libuv, epoll) avoid the
locks, but they turn sequential logic into callbacks and state machines.

## Processes

A CLPF **process** is a unit of execution with:

- its own **stack** (a coroutine: 16 KiB by default),
- its own **heap**, released all at once when the process ends,
- its own **mailbox**,
- a **pid** that names it, and a priority.

A process is cheap: a few microseconds and a few kilobytes to create. The
runtime runs them **M:N**: many processes on a few OS threads (*schedulers*),
one per core minus two. You don't create threads; you create as many processes
as your problem has independent activities: one per connection, per job, per
device, per entity of a simulation.

Each process is written as plain sequential code:

```c
TASK(connection, int fd) {
    while (1) {
        Message *msg = RECEIVE(NO_TAG); // wait, without blocking a thread
        /* ... handle it ... */
        MSG_FREE(msg);
    }
}
```

## Messages and mailboxes

Processes share **nothing**. The only way to affect another process is to
**send it a message**:

- `SEND(pid, data, size, tag)` **copies** `size` bytes into the receiver's
  heap and queues them in its mailbox. The sender keeps its own data, and the
  receiver owns the copy, so no lock is needed anywhere.
- Each message has a **tag**: an integer, a string, a notification name...
  `RECEIVE(tag)` takes the *oldest message with that tag* and leaves the
  others in the mailbox. This is *selective receive*: a process waiting for a
  reply is not disturbed by unrelated messages arriving in the meantime.
- Messages from one sender to one receiver arrive in the order they were sent.

Because data is copied, a pointer inside a payload means nothing to the
receiver: send values, not addresses into your own memory.

## Let it crash

In a large system, unexpected failures happen. Instead of checking every
corner case defensively, a process that finds itself in a state it does not
know how to handle **ends**: `EXIT_ERROR()`. Recovery is someone else's job,
and that someone is told:

- **Monitors**: `MONITOR(pid)` asks for a *DOWN* message when `pid` ends,
  with the reason. It is one-way and does not affect the watcher.
- **Links**: `LINK(pid)` ties two processes together. When one ends
  abnormally, the other ends too, unless it *traps exits* and gets a message
  instead. Links are how groups of processes that cannot work without each
  other fail together.
- **Supervisors** are processes whose only job is to start children, watch
  them, and restart them according to a *strategy* when they fail.

Since a process's memory is private, a crash cannot corrupt anybody else.
Restarting it gives a clean state.

## Supervision trees

Supervisors can supervise other supervisors, which gives a tree:

```text
            app (supervisor, ONE_FOR_ONE)
           /                         \
   database pool (ONE_FOR_ALL)    http (supervisor, REST_FOR_ONE)
     |        |                     |          |
   conn 1   conn 2               listener   acceptor pool
```

A failure is handled at the lowest level that can fix it. If a level fails
too often (its *restart intensity*: more than N restarts in T milliseconds),
it gives up, stops its children and fails itself, and the level above takes
over. The root of the tree is your entry process: when it ends, the program
ends.

## Scheduling

Scheduling is **cooperative with preemption points**. A process gives the CPU
back when:

- it waits in `RECEIVE` (or `SLEEP`, `RECEIVE_TIMEOUT`...),
- it calls `YIELD()`,
- it has used up its **reduction budget**: runtime calls (`SEND`, `FETCH`...)
  and `REDUCE(n)` spend it, and once it runs out the process yields
  automatically.

The runtime cannot interrupt a C function that never calls it. A long
computation should call `REDUCE` regularly, as Erlang code naturally does.
Idle schedulers **steal** ready processes from busy ones, and they sleep
instead of spinning when there is no work at all.

## How it compares

| | pthreads + locks | Event loop (libuv...) | Erlang/OTP | CLPF |
|---|---|---|---|---|
| Unit of concurrency | OS thread (expensive) | Callback | Process (cheap) | Process (cheap) |
| Communication | Shared memory | Shared state, callbacks | Copied messages | Copied messages |
| Code style | Sequential, with locks | Callbacks / state machines | Sequential | Sequential |
| Failure isolation | None | None | Per process | Per process |
| Recovery | Manual | Manual | Supervisors | Supervisors |
| Preemption | OS | None | Reductions (always) | Reductions (in runtime calls) |
| Language | C | C | Erlang/Elixir | C |

CLPF is not a replacement for the BEAM. There is no distribution across
machines, no hot code loading, and C code can still corrupt memory inside its
own process. What it gives you is the programming model: structure a C program
as isolated, supervised, message-passing processes.

## Vocabulary

| Term | Meaning |
|---|---|
| Process | A lightweight task with its own stack, heap and mailbox, identified by a pid |
| Pid | Process identifier. A dead process's pid is never reused for another process |
| Scheduler | An OS thread running processes |
| Mailbox | The queue of messages waiting to be received by a process |
| Tag | The label of a message, used to receive it selectively |
| DOWN | The message a monitor receives when the watched process ends |
| Link | A bidirectional tie: an abnormal exit of one ends the other |
| Exit signal | What travels along a link, or what `KILL`/`SEND_EXIT` send |
| Trap exit | Receiving exit signals as messages instead of dying from them |
| Supervisor | A process that starts, watches and restarts children |
| Restart intensity | How many restarts a supervisor accepts within a period |
| Reduction | A unit of work used to decide when a process must yield |
| Entry process | The task given to `CLPF_ENTRY`; the program ends with it |
