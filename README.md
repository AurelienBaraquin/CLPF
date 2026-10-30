# CLPF: Concurrent Lightweight Process Framework

[![CI](https://github.com/AurelienBaraquin/CLPF/actions/workflows/ci.yml/badge.svg)](https://github.com/AurelienBaraquin/CLPF/actions/workflows/ci.yml)
[![Docs](https://github.com/AurelienBaraquin/CLPF/actions/workflows/docs.yml/badge.svg)](https://aurelienbaraquin.github.io/CLPF/)

**Erlang-style concurrency for C.** CLPF lets you write a C program as
thousands of lightweight *processes* that share nothing, communicate only by
*messages*, and are restarted by *supervisors* when they fail. The runtime
runs them in parallel on all your cores.

**[Documentation](https://aurelienbaraquin.github.io/CLPF/)** ·
[Getting started](https://aurelienbaraquin.github.io/CLPF/getting-started/) ·
[API reference](https://aurelienbaraquin.github.io/CLPF/api/)

## Why

Concurrent C usually means threads, shared memory and locks: data races,
deadlocks, and a single crash taking the whole program down. Erlang showed a
better way, and CLPF brings it to C:

- **Processes instead of threads:** each has its own stack, heap and mailbox,
  costs a few kilobytes, and is written as plain sequential code.
- **Messages instead of shared memory:** data is copied to the receiver, which
  picks messages out of its mailbox by tag. User code needs no locks.
- **Let it crash:** a process that fails just ends. Monitors, links and
  supervision trees detect it and restart it in a clean state.

It fits programs made of many independent activities: servers, pipelines,
job systems, simulations, device or connection managers.

## Example

```c
#include <api/task.h>
#include <api/message.h>
#include <api/io.h>
#include <api/supervisor.h>

TASK(worker, Pid boss) {
    PRINT("worker %ld up\n", self->pid);
    SEND(boss, NULL, 0, MAKE_STR_TAG("up"));
    EXIT_ERROR(); // crash: the supervisor restarts a fresh copy
}

TASK(entry, int argc, char *argv[]) {
    Pid sup = SPAWN_SUPERVISOR("sup", SUP_ONE_FOR_ONE, 1000 /* ms */, 3 /* restarts */);
    SUP_ADD_CHILD(sup, RES_PERMANENT, worker, "worker", 1, self->pid);

    for (int i = 0; i < 3; ++i) // the first start and two restarts
        MSG_FREE(RECEIVE(MAKE_STR_TAG("up")));
}

CLPF_ENTRY(entry) // main(): the program ends when entry does
```

## Quick start

```sh
git clone --recursive https://github.com/AurelienBaraquin/CLPF.git
cd CLPF
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release && cmake --build build -j
cmake --install build --prefix ~/.local
gcc -std=gnu11 app.c $(PKG_CONFIG_PATH=~/.local/lib/pkgconfig pkg-config --cflags --libs clpf) -o app
```

With CMake: `find_package(clpf)` and link `clpf::clpf`. Run `./build/clpf_demo`
for a showcase of the whole API, and see [`examples/`](examples) for short
programs.

## Features

- **Processes:** `SPAWN` with arguments or a copied data block, priorities,
  per-process stack size with overflow reports, up to 65 536 at once.
- **Messages:** `SEND`, selective `RECEIVE` by tag or matcher, `REQUEST_REF`/`REPLY`
  with unique references, notifications, timeouts, mailbox limits.
- **Failure handling:** `MONITOR` (DOWN messages), `LINK`, `TRAP_EXIT`, `KILL`.
- **Supervision:** `ONE_FOR_ONE`, `ONE_FOR_ALL`, `REST_FOR_ONE`;
  permanent, transient and temporary children; restart intensity.
- **Services:** name registry, one-shot and periodic timers, ordered output (`PRINT`), non-blocking
  stdin (`GETLINE`), sockets and pipes (`IO_WAIT`, `IO_WATCH`), leveled logs.
- **Observability:** `PROCESS_INFO`, `PROCESS_LIST`, a process dump on
  `SIGUSR1`, a watchdog for processes that never yield, benchmarks.
- **Runtime:** M:N work-stealing schedulers, reduction-based preemption, no
  CPU used when idle, clean shutdown with an exit code.

## Status

**Experimental, feature-complete for its scope.** Everything above works and
is covered by 45 tests. The CI runs them in Debug and Release, and under
AddressSanitizer and ThreadSanitizer.

Limits:

- x86-64 Linux only: the context switch (libaco) and the argument
  trampoline are x86-64 assembly.
- Preemption happens in runtime calls only: long computations must call
  `REDUCE`.
- Needs GCC or Clang (GNU C statement expressions).
- The API may still change.

## Documentation

| | |
|---|---|
| [Getting started](https://aurelienbaraquin.github.io/CLPF/getting-started/) | Install, first program, first supervisor |
| [Concepts](https://aurelienbaraquin.github.io/CLPF/concepts/) | The actor model and why it fits C |
| [Guides](https://aurelienbaraquin.github.io/CLPF/guides/processes/) | Processes, messages, failures, supervision, timers, registry, I/O, memory, scheduling |
| [Patterns](https://aurelienbaraquin.github.io/CLPF/patterns/) | Servers, worker pools, pipelines, moving from threads |
| [API reference](https://aurelienbaraquin.github.io/CLPF/api/) | Every macro, type and constant |
| [Architecture](https://aurelienbaraquin.github.io/CLPF/architecture/) | How the runtime works inside |
| [Building and contributing](https://aurelienbaraquin.github.io/CLPF/building/) | Options, tests, sanitizers, CI |
| [Troubleshooting and FAQ](https://aurelienbaraquin.github.io/CLPF/troubleshooting/) | Common mistakes and questions |

The sources of the site are in [`docs/`](docs).

## License

GPL-3.0. See [LICENSE](LICENSE).
