# Processes

Headers: `api/base.h`, `api/task.h`

## Declaring a process body

A process runs a *task*: a function declared with one of these macros. Each
gives the function an implicit first parameter, `Process *self`, which every
CLPF macro uses.

```c
TASK0(ticker) { /* no arguments */ }

TASK(worker, Pid boss, int id) { /* up to 5 integer or pointer arguments */ }

typedef struct { double ratio; char label[16]; } Settings;
TASK_WITH(configured, Settings, settings) { /* settings is a Settings * */ }
```

Ordinary helper functions can use the macros too, if they receive `self`:
`DEF` declares such a function and `CALL` calls it.

```c
static int DEF(ask_counter, Pid counter) {
    Message *reply = REQUEST(counter, NULL, 0, MAKE_STR_TAG("get"));
    int value = *(int *)reply->payload;
    MSG_FREE(reply);
    return value;
}

TASK(client, Pid counter) {
    int value = CALL(ask_counter, counter);
    (void)value;
}
```

`self` gives access to a few fields you may read: `self->pid`, `self->name`
(the spawn label) and `self->scheduler_pid` (the scheduler running it, which
can change).

## Spawning

Every spawn is synchronous: it returns the new pid (or `INVALID_PID`) before
the child can run. The child goes to the least loaded scheduler.

| Macro | Use |
|---|---|
| `SPAWN(func, argc, args...)` | Up to 5 integer or pointer arguments |
| `SPAWN0(func)` | A `TASK0` |
| `SPAWN_WITH(func, &data, sizeof data)` | A `TASK_WITH`: the block is copied into the child's heap |
| `SPAWN_NAMED`, `SPAWN0_NAMED` | With a label, shown in logs |
| `SPAWN_PRIO`, `SPAWN0_PRIO`, `SPAWN_PRIO_NAMED`, `SPAWN0_PRIO_NAMED` | With a priority |
| `SPAWN_LINK(func, argc, args...)` | Linked to the spawner before it runs (see [failures](failures.md)) |
| `SPAWN_OPTS(func, .field = value...)` | Any `SpawnOptions` |

!!! warning "Arguments are values"
    `SPAWN` arguments are passed as machine words: integers and pointers
    only, no `double` and no struct. A pointer you pass points into **your**
    memory: the child must not keep it past your own lifetime, and the two of
    you must not modify the data at the same time. To hand over data, use
    `SPAWN_WITH`, which copies it, or send it in a message.

```c
#include <api/task.h>
#include <api/message.h>
#include <api/io.h>

typedef struct {
    double x, y;
    char name[16];
    Pid report_to;
} Point;

TASK_WITH(describe, Point, p) {
    PRINT("%s is at (%.1f, %.1f)\n", p->name, p->x, p->y);
    SEND(p->report_to, NULL, 0, MAKE_STR_TAG("done"));
}

TASK(entry, int argc, char *argv[]) {
    Point p = {1.5, -2.0, "origin+", self->pid};
    SPAWN_WITH(describe, &p, sizeof p); // p is copied: it may change or vanish now
    MSG_FREE(RECEIVE(MAKE_STR_TAG("done")));
}

CLPF_ENTRY(entry)
```

### SpawnOptions

`SPAWN_OPTS` and `clpf_spawn(self, &funcall, options)` take a `SpawnOptions`.
Fields you leave out take their default:

| Field | Default | Meaning |
|---|---|---|
| `name` | `""` | Label |
| `priority` | `PROCESS_PRIORITY_MEDIUM` | See below |
| `monitor` | `false` | Monitor the child from its very start |
| `link` | `false` | Link to the child before it runs |
| `trap_exit` | `false` | Trap exit signals from the start: one sent before the child first runs arrives as a message instead of killing it |
| `arg_block`, `arg_block_size` | none | Data copied into the child, passed as its argument |
| `stack_size` | 16 KiB | Stack size |
| `mailbox_limit` | `0` (none) | Messages that may wait; past it `SEND` returns `SEND_FULL` |
| `max_heap_size` | `0` (none) | Bytes its heap may take; past it, the process ends with `EXIT_REASON_HEAP_LIMIT` (see [memory](memory.md#heap-limit)) |
| `scheduler` | `0` | `1 +` the index of the scheduler to start on; `0` picks the least loaded |

```c
Pid pid = SPAWN_OPTS(big_recursion, .name = "parser", .stack_size = 256 * 1024, .monitor = true);
```

## Priorities

| Priority | Behaviour |
|---|---|
| `PROCESS_PRIORITY_VERY_HIGH` | Runs before anything else on its scheduler |
| `PROCESS_PRIORITY_HIGH` | Runs before medium and low |
| `PROCESS_PRIORITY_MEDIUM` | Default |
| `PROCESS_PRIORITY_LOW` | Shares the medium queue but gets one turn for every 8 of a medium process |

A low priority process is never starved: when it is the only ready process,
it runs. Use high priorities sparingly; they are meant for short, latency
sensitive processes (the I/O server is `HIGH`).

## Parents and children

The spawner is the child's *parent*. `PARENT()` returns it; `NB_CHILDREN()`
and `CHILD(i)` list the live children. This relationship is informational:
a child does not die with its parent unless they are [linked](failures.md).

## Ending a process

A process ends when:

- its task function returns: a normal exit;
- it calls `EXIT_NORMAL()`, `EXIT_ERROR()` or `EXIT(reason)`. These never
  return, and code after them does not run;
- it is killed (`KILL`), or an exit signal reaches it through a
  [link](failures.md);
- the program shuts down.

The exit reason is what monitors and links see:

| Reason | Meaning |
|---|---|
| `EXIT_REASON_NORMAL` | Finished its work |
| `EXIT_REASON_ERROR` | Failed |
| `EXIT_REASON_KILLED` | Ended by `KILL` |
| `EXIT_REASON_SHUTDOWN` | Stopped by its supervisor or by the runtime |
| `EXIT_REASON_HEAP_LIMIT` | Its heap would have grown past its `max_heap_size` |
| `EXIT_REASON_NOPROC` | Only in DOWN messages: the process did not exist when it was monitored |

When a process ends, its heap and stack are released at once, its registered
name is freed, and its monitors and links are notified.

## Hibernating

A process that waits a long time between messages still holds its stack.
`HIBERNATE(func, argc, args...)` drops it: the process gives its stack back,
and the heap pools and pages it no longer uses, then waits for a message.
When one comes, it starts again in `func` with these arguments, like a new
`SPAWN`, but it keeps its pid, mailbox, links, monitors and registered name.
`HIBERNATE0(func)` takes no arguments. Neither returns.

Everything on the stack is lost, so the state the process needs must be in
its arguments, or in its heap behind a pointer passed as one:

```c
#include <api/task.h>
#include <api/message.h>
#include <api/io.h>

// A counter that sleeps between requests: its state is its argument
TASK(counter, int total) {
    Message *msg = RECEIVE(NO_TAG); // a request from REQUEST_REF
    total += *(int *)msg->payload;
    REPLY(msg, &total, sizeof total);
    MSG_FREE(msg);
    HIBERNATE(counter, 1, total);
}

TASK(entry, int argc, char *argv[]) {
    Pid pid = SPAWN(counter, 1, 0);
    for (int i = 1; i <= 3; ++i) {
        Message *reply = REQUEST_REF(pid, &i, sizeof i, 1000);
        PRINT("total: %d\n", *(int *)reply->payload);
        MSG_FREE(reply);
    }
}

CLPF_ENTRY(entry)
```

Any message wakes a hibernating process, including one it then leaves in its
mailbox. A hibernating process costs about 12 KiB, the allocator at the
start of its heap, against about 17 KiB for one waiting in `RECEIVE`, and
waking it costs a coroutine start. Hibernate processes that wait seconds,
not microseconds.

## The entry process and the program's exit

`CLPF_ENTRY(task)` generates `main()`. `task` receives `(int argc, char
*argv[])` and is the program's **entry process**. When it ends, whatever the
reason:

1. every other user process is killed;
2. the runtime's own processes stop, after writing all pending `PRINT` output;
3. `main()` returns **0** if the entry process exited normally, **1** otherwise.

<!-- expect-exit: 1 -->
```c
#include <api/task.h>
#include <api/io.h>

TASK0(forever) {
    while (1)
        YIELD();
}

TASK(entry, int argc, char *argv[]) {
    SPAWN0(forever); // does not keep the program alive
    if (argc < 2) {
        PRINT("usage: %s <file>\n", argv[0]);
        EXIT_ERROR(); // the program exits with code 1
    }
}

CLPF_ENTRY(entry)
```

## Stacks

A process stack is 16 KiB by default, enough for ordinary code but not for
deep recursion or large local arrays. Give a process more with
`SpawnOptions.stack_size`. To change the default for every process, build
CLPF with `-DCLPF_DEFAULT_STACK_SIZE=...` in `CMAKE_C_FLAGS`.

Overflowing the stack is still a crash, but the runtime tells you which
process did it:

```text
clpf: stack overflow in process 'parser' (pid 12), give it a larger SpawnOptions.stack_size
```
