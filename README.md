# CLPF: Concurrent Lightweight Process Framework

**An Erlang/BEAM-style actor runtime written in C.** It multiplexes lightweight processes across a pool of scheduler threads. Processes talk only through mailboxes, and supervisors restart them when they crash.

> Status: experimental. See [Status](#status) for what works and the known limits.

## Features

- **Lightweight processes:** each one is a coroutine with its own stack (16 KiB by default, configurable per spawn, overflows are reported by process name), a private heap, a mailbox and a priority. There can be up to 65,536 processes at once. Arguments are up to 5 integers, or any block of data copied with `SPAWN_WITH`.
- **M:N scheduling:** processes are multiplexed onto one OS thread per usable core, minus 2 (at least one), and idle schedulers steal work from busy ones. A process gives up the CPU on `YIELD()`, a blocking `RECEIVE()`, or once it spent its reduction budget (runtime calls and `REDUCE(n)` spend it). C code that never calls the runtime cannot be preempted.
- **Message passing with selective receive:** a message (payload and tag string included) is copied into the receiver's heap, so pointers inside a payload mean nothing to the receiver. `RECEIVE(tag)` returns the oldest message with that tag, `RECEIVE_IF(TM_PREFIX | TM_GTE | TM_LTE | TM_IN_RANGE | TM_KIND, pattern)` the oldest accepted by a matcher, `FETCH`/`FETCH_IF` the same without blocking; release a message with `MSG_FREE`. Tags can be ints, strings, notification names or internal codes.
- **Supervision trees:** `SPAWN_SUPERVISOR` + `SUP_ADD_CHILD`, with `ONE_FOR_ONE`, `ONE_FOR_ALL` and `REST_FOR_ONE` strategies and permanent, transient and temporary children. Past its restart intensity (max N restarts per period, all children together) a supervisor stops its children and exits with an error.
- **Links and exits:** `KILL(pid)`, `LINK` / `UNLINK` / `SPAWN_LINK` (an abnormal exit takes linked processes down), `TRAP_EXIT(true)` to receive exits as messages, `SEND_EXIT(pid, reason)`.
- **Timers:** `SEND_AFTER`, `CANCEL_TIMER`, `RECEIVE_TIMEOUT`, `SLEEP`.
- **Monitors:** `MONITOR(pid)` makes a `DOWN` message (tag `MAKE_DOWN_TAG(pid)`, payload `DownInfo { from, reason }`) arrive when the process exits, or right away with `EXIT_REASON_NOPROC` if it is already gone. Supervisors are built on this.
- **Name registry:** `REGISTER(name, pid)` / `WHEREIS` / `UNREGISTER`, synchronous, one name per process, released at exit. Spawn names are only labels.
- **Built-in system processes:** a launcher boots a supervised I/O server; `PRINT` goes through it, so output from different threads never interleaves. `GETLINE` reads stdin on a dedicated thread, blocking nothing else.
- **Clean shutdown:** the program ends when the entry task ends: user processes are killed, queued output is written, and the exit code is 0 only if the entry task exited normally.

## Quick example

```c
#include <api/task.h>
#include <api/message.h>
#include <api/io.h>
#include <api/supervisor.h>

TASK(pong, Pid parent) {
    Message *ping = RECEIVE(MAKE_STR_TAG("ping"));      // blocks (yields) until a matching message
    int reply = *(int *)ping->payload + 1;
    MSG_FREE(ping);                                     // messages live in the receiver's heap
    SEND(parent, &reply, sizeof reply, MAKE_STR_TAG("pong"));
    EXIT_NORMAL();
}

TASK(worker, Pid parent) {
    PRINT("%s started, crashing\n", self->name);
    SEND(parent, NULL, 0, MAKE_STR_TAG("started"));
    EXIT_ERROR();                                       // the supervisor restarts it
}

TASK(entry, int argc, char *argv[]) {
    Pid p = SPAWN(pong, 1, self->pid);                  // SPAWN(fn, argc, args...), up to 5 args
    int n = 41;
    SEND(p, &n, sizeof n, MAKE_STR_TAG("ping"));
    Message *reply = RECEIVE(MAKE_STR_TAG("pong"));
    PRINT("entry got %d\n", *(int *)reply->payload);    // -> entry got 42
    MSG_FREE(reply);

    Pid sup = SPAWN_SUPERVISOR("sup", SUP_ONE_FOR_ONE, 1000 /* ms */, 3 /* max restarts */);
    SUP_ADD_CHILD(sup, RES_PERMANENT, worker, "worker", 1, self->pid);

    for (int i = 0; i < 1 + 3; ++i)                     // first start + 3 restarts
        MSG_FREE(RECEIVE(MAKE_STR_TAG("started")));
    EXIT_NORMAL();                                      // the program ends with its entry task
}

CLPF_ENTRY(entry)                                       // generates main(): exit code 0 or 1
```

`TASK` / `TASK0` / `TASK_WITH` declare a process body. Every body gets an implicit `Process *self`, and every macro uses it. `EXIT_NORMAL()` / `EXIT_ERROR()` end the process on the spot; returning from the body is a normal exit.

## Architecture

- **Context switching:** processes are [libaco](https://github.com/hnes/libaco) coroutines (included as a git submodule), so switching between them is a small assembly register swap with no syscall. A process's arguments are passed through `funcall.S`. This hand-written x86-64 trampoline loads up to 6 integer arguments into the System V argument registers, then calls the entry point. `self` is always the first argument.
- **Schedulers:** each scheduler thread owns 3 private run queues (very high, high, medium/low). A low-priority process is picked once every 8 passes over its queue. When none of its processes is ready, a scheduler sleeps on a condition variable and is woken by whoever gives it work: no polling.
- **Cross-thread handoff:** other threads push onto intrusive lock-free MPSC queues (a CAS-push Treiber stack whose node lives in the queued element, drained with a single atomic exchange and reversed for FIFO order). Pushing never allocates.
- **Wakeups:** senders never touch another process's state. Waiting processes are in no queue; a delivery pushes the receiver once onto its scheduler's wakeup queue, so a scheduler only looks at processes that actually received something, however many are blocked.
- **Spawning:** a spawn runs directly on the calling thread (slot allocation and the name registry are guarded by a table lock) and places the process on the least loaded scheduler. `SPAWN` never yields.
- **Control plane:** the main thread destroys exited processes, fires timers (a min-heap, sleeping until the next deadline) and drives the shutdown; spawning and the registry run on the caller's thread under a table lock.
- **Work stealing:** an idle scheduler asks the one with the most ready processes for work; at its next pick that one hands half of them over (updating their scheduler and pointing their coroutine back to the new thread). Idle schedulers spin briefly before sleeping, so quick replies do not pay for a wakeup.
- **Kills:** `KILL` only records a pending kill under the target's slot lock; the target's scheduler ends it the next time it would run it, without resuming its coroutine.
- **Pids:** a pid is a process table index tagged with a per-slot generation, so the pid of a dead process never designates a later one. Each slot has a lock that senders hold while delivering, so a message either reaches a live process or `SEND` returns -1.
- **Memory:** each process has its own [TLSF](http://www.gii.upv.es/tlsf/) heap on `mmap`'d pools, which grow on demand. A spinlock guards the heap, because senders on other threads allocate into it. When a process exits, its whole heap is unmapped at once, with no per-object cleanup.
- **Mailboxes:** a two-stage design. The inbox takes writes from any thread under the receiver's slot lock. A private "seen" list lets the owning process scan for selective receive without taking the lock.
- **Monitors:** an exiting process detaches its watcher list under its slot lock and delivers a `DOWN` message to each watcher. Supervisors spawn and monitor a child in one step, so a child that crashes immediately is still seen.
- **Messages:** one allocation per message in the receiver's heap: header, 16-byte aligned payload, then the copied tag string.

## Build & run

Requirements: Linux on x86-64, GCC or Clang (the API uses GNU C statement expressions), CMake ≥ 3.15, pthreads.

```sh
git clone --recursive https://github.com/AurelienBaraquin/CLPF.git
cd CLPF                       # if already cloned: git submodule update --init
cmake -S . -B build
cmake --build build -j
./build/clpf_demo             # the showcase in main.c (-i to type your name)
ctest --test-dir build --output-on-failure
```

[`docs/api.md`](docs/api.md) documents the whole API, and [`examples/`](examples) holds short runnable programs (ping-pong through the registry, supervisors, timers, links).

### Using the library

```sh
cmake --install build --prefix ~/.local
```

installs `libclpf.a` and its vendored dependencies, the headers under `include/clpf`, a CMake package and a pkg-config file:

```cmake
find_package(clpf REQUIRED)
target_link_libraries(my_app clpf::clpf)
```

```sh
gcc -std=gnu11 my_app.c $(pkg-config --cflags --libs clpf) -o my_app
```

### Options and checks

CMake options:

- `CMAKE_BUILD_TYPE` (default `Debug`)
- `CLPF_SANITIZE=thread` or `address` builds everything, tests included, with that sanitizer
- `CLPF_TRACE=ON` logs every process creation and destruction to stdout
- `CLPF_BUILD_TESTS`, `CLPF_BUILD_EXAMPLES` (both `ON` by default)

```sh
cmake -S . -B build-thread -DCLPF_SANITIZE=thread
cmake --build build-thread -j
setarch -R ctest --test-dir build-thread    # setarch -R: recent kernels need ASLR off for TSan
```

Tests live in `tests/`. Each one is a small CLPF program that reports through `TEST_ASSERT` / `TEST_PASS` (`tests/test_helpers.h`), and CTest treats a hang as a failure through its timeout.

The code is formatted with clang-format and checked with clang-tidy (`.clang-format`, `.clang-tidy`). The CI runs the tests in Debug and Release, under both sanitizers, builds an example against the installed package, and checks formatting and clang-tidy with pinned versions (`pip install clang-format==23.1.3 clang-tidy==23.1.4`).

## Status

Working (checked on x86-64 Linux):

- spawning processes with arguments and priorities, on several scheduler threads
- message passing with selective receive by tag; request/reply
- supervisors with the three strategies and a restart intensity, links, kills and trapped exits
- monitors / `DOWN` messages, the name registry, timers, the supervised I/O server and stdin reader
- reduction-based preemption, work stealing, clean shutdown with an exit code
- a regression test suite, clean under AddressSanitizer and ThreadSanitizer

Limits:

- x86-64 Linux only: libaco's context switch and the `funcall.S` trampoline are x86-64 assembly.
- Preemption only happens in runtime calls (and `REDUCE`): a tight C loop keeps its scheduler.

## License

GPL-3.0. See [LICENSE](LICENSE).
