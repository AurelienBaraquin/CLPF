# CLPF: Concurrent Lightweight Process Framework

**An Erlang/BEAM-style actor runtime written in C.** It multiplexes lightweight processes across a pool of scheduler threads. Processes talk only through mailboxes, and supervisors restart them when they crash.

> Status: experimental, paused since May 2025. The core runtime works. See [Status](#status) for what is and isn't done.

## Features

- **Lightweight processes:** each one is a coroutine with its own 16 KiB stack, a private heap, a mailbox and a priority. There can be up to 65,536 processes.
- **M:N scheduling:** processes are multiplexed onto one OS thread per core (cores − 2). Scheduling is cooperative. A process gives up the CPU on `YIELD()`, `SEND()` or a blocking `RECEIVE()`.
- **Message passing with selective receive:** payloads are copied into the receiver's heap. `RECEIVE(tag)` waits for the first message whose tag matches. Tags can be ints, strings or internal codes, with matchers for prefix, range and kind.
- **Supervision trees:** `SPAWN_SUPERVISOR` + `SUP_ADD_CHILD`. Restart limits use a sliding window (max N restarts per period).
- **Monitors:** a process can watch another one and gets a `DOWN` callback with the exit reason when it dies. Supervisors are built on this.
- **Name registry:** `REGISTER` / `WHEREIS` / `UNREGISTER`, backed by a trie.
- **Built-in system processes:** a launcher boots a supervised I/O server. `PRINT` / `GETLINE` go through it, so output from different threads never interleaves.

## Quick example

```c
#include <api/task.h>
#include <api/message.h>
#include <api/io.h>
#include <api/supervisor.h>
#include <flags_internal.h>

TASK(pong, Pid parent) {
    int *n = RECEIVE(MAKE_STR_TAG("ping"));       // blocks (yields) until a matching message
    int reply = *n + 1;
    SEND(parent, &reply, sizeof reply, MAKE_STR_TAG("pong"));
    FREE(n);                                      // payloads live in the receiver's heap
    EXIT_NORMAL();
}

TASK0(worker) {
    PRINT("%s started, crashing\n", self->__name);
    EXIT_ERROR();                                 // the supervisor restarts it
}

TASK(entry, int argc, char *argv[]) {
    Pid p = SPAWN(pong, 1, self->pid);            // SPAWN(fn, argc, args...), up to 5 args
    int n = 41;
    SEND(p, &n, sizeof n, MAKE_STR_TAG("ping"));
    int *r = RECEIVE(MAKE_STR_TAG("pong"));
    PRINT("entry got %d\n", *r);                  // -> entry got 42
    FREE(r);

    Pid sup = SPAWN_SUPERVISOR("sup", SUP_ONE_FOR_ONE, 1000 /* ms */, 3 /* max restarts */);
    SUP_ADD_CHILD0(sup, RES_PERMANENT, worker, "worker");

    for (int i = 0; i < 100; ++i) YIELD();
    exit_in_progress = true;                      // ask the runtime to shut down
    EXIT_NORMAL();
}

CLPF_ENTRY(entry)                                 // generates main()
```

`TASK` / `TASK0` declare a process body. Every body gets an implicit `Process *self`, and every macro uses it. A process must end with `EXIT_NORMAL()` or `EXIT_ERROR()`.

## Architecture

- **Context switching:** processes are [libaco](https://github.com/hnes/libaco) coroutines (included as a git submodule), so switching between them is a small assembly register swap with no syscall. A process's arguments are passed through `funcall.S`. This hand-written x86-64 trampoline loads up to 6 integer arguments into the System V argument registers, then calls the entry point. `self` is always the first argument.
- **Schedulers:** each scheduler thread owns 4 private run queues (very high, high, medium/low, waiting). A low-priority process is picked once for every 8 times a normal one is. Waiting processes move back to the ready queues when a message reaches them.
- **Cross-thread handoff:** other threads push onto lock-free MPSC queues (a CAS-push Treiber stack, drained with a single atomic exchange and reversed for FIFO order). The owning scheduler moves them into its private queues before each pick.
- **Control plane:** spawn, exit, register and lookup requests are posted to a global MPSC queue. The main thread processes them and replies with a message. A new root process goes to the least-loaded scheduler; a child process stays on its parent's scheduler.
- **Memory:** each process has its own [TLSF](http://www.gii.upv.es/tlsf/) heap on `mmap`'d pools, which grow on demand. A spinlock guards the heap, because senders on other threads allocate into it. When a process exits, its whole heap is unmapped at once, with no per-object cleanup.
- **Mailboxes:** a two-stage design. A mutex-protected inbox takes writes from any thread. A private "seen" list lets the owning process scan for selective receive without taking the lock.
- **Signals and monitors:** a process's exit sends `DOWN` signals through a per-process MPSC signal queue. The scheduler turns them into messages, and they run the monitor callback the next time the target is scheduled.

## Build & run

Requirements: Linux on x86-64, GCC or Clang, CMake ≥ 3.15, pthreads. The machine needs 3 or more cores, because the runtime keeps 2 cores out of the scheduler pool.

```sh
git clone --recursive https://github.com/AurelienBaraquin/CLPF.git
cd CLPF                       # if already cloned: git submodule update --init
cmake -S . -B build
cmake --build build -j
./build/cbeam_main            # demo: a supervised child that crashes and is restarted
```

The build produces `build/libcbeam.a` plus `libtlsf.a`, `liblibaco.a` and `libstb_sprintf.a`. To build your own program against them:

```sh
gcc -std=gnu11 -Iinclude -Iexternal my_app.c \
    -Lbuild -lcbeam -ltlsf -llibaco -lstb_sprintf -lpthread -o my_app
```

The build uses debug flags by default (`-O0 -g3`), and the runtime logs every process creation and destruction to stdout.

## Status

Working (checked on x86-64 Linux):

- spawning processes with arguments and priorities, on several scheduler threads
- message passing with selective receive by tag; request/reply
- `ONE_FOR_ONE` supervisors with `RES_PERMANENT` restarts and a restart-intensity limit
- monitors / `DOWN` notifications, the name registry, the supervised I/O server

Not finished / planned (from the project's `todo`):

- [ ] `ONE_FOR_ALL` and `REST_FOR_ONE` strategies. The code paths exist but are not functional yet.
- [ ] Honouring `RES_TRANSIENT` / `RES_TEMPORARY` from `SUP_ADD_CHILD`. Every child is currently registered as permanent.
- [ ] Load balancing: migrating processes and work stealing between schedulers, and choosing a better scheduler when spawning a process (the scheduler struct already has fields reserved for migration).
- [ ] A supervisor for the system servers in the launcher (so the I/O server is always running).
- [ ] Shutting down cleanly when the application tree ends. Right now the entry process sets `exit_in_progress`.
- [ ] Event-driven idle. Schedulers currently poll every 1 ms instead of sleeping on their condition variable.
- [ ] Preemption based on reduction counting, as BEAM does. Scheduling is purely cooperative today.
- [ ] A test suite.

## License

GPL-3.0. See [LICENSE](LICENSE).
