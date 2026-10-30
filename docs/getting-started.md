# Getting started

This page takes you from a fresh clone to a running program with a few
processes, one of which crashes and gets restarted.

## Requirements

- Linux on x86-64
- GCC or Clang. The API uses GNU C statement expressions: `-std=gnu11` is
  recommended, and `-pedantic` would warn about them.
- CMake 3.15 or newer
- `pkg-config`, if you want to build with it

## Build and install

```sh
git clone --recursive https://github.com/AurelienBaraquin/CLPF.git
cd CLPF
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j
ctest --test-dir build              # optional: runs the test suite
cmake --install build --prefix ~/.local
```

`--recursive` fetches [libaco](https://github.com/hnes/libaco), the coroutine
library CLPF is built on. If you already cloned without it, run
`git submodule update --init`.

The install puts the following under `~/.local`:

| Path | Content |
|---|---|
| `lib/libclpf.a` | The runtime |
| `lib/libclpf_aco.a`, `lib/libclpf_tlsf.a`, `lib/libclpf_stb_sprintf.a` | Vendored dependencies |
| `include/clpf/` | Headers; the API is in `include/clpf/api/` |
| `lib/cmake/clpf/` | CMake package |
| `lib/pkgconfig/clpf.pc` | pkg-config file |

## Your first program

Create `hello.c`:

```c
#include <api/task.h>
#include <api/message.h>
#include <api/io.h>

// A process body. `self` is the running process, implicit in every macro.
TASK(greeter, Pid reply_to, int number) {
    char text[32];
    int length = snprintf(text, sizeof text, "hello from #%d", number);
    SEND(reply_to, text, (size_t)length + 1, MAKE_STR_TAG("greeting"));
}

TASK(entry, int argc, char *argv[]) {
    for (int i = 1; i <= 3; ++i)
        SPAWN(greeter, 2, self->pid, i);

    for (int i = 0; i < 3; ++i) {
        Message *msg = RECEIVE(MAKE_STR_TAG("greeting"));
        PRINT("%s\n", (char *)msg->payload);
        MSG_FREE(msg);
    }
}

CLPF_ENTRY(entry)
```

Here is what happens:

1. `CLPF_ENTRY(entry)` generates `main()`. It starts the runtime (one
   scheduler thread per core, minus two, at least one) and runs `entry` as the
   first user process, with `argc` and `argv`.
2. `SPAWN(greeter, 2, self->pid, i)` creates a new process that runs
   `greeter(self->pid, i)`. It returns at once with the new pid; the child
   runs concurrently, probably on another core.
3. `SEND` copies the 15-byte string into the receiver's mailbox. The child's
   `text` buffer can disappear right after: the receiver has its own copy.
4. `RECEIVE(tag)` blocks `entry` until a message with that tag arrives. Only
   `entry` is blocked: its thread runs other processes meanwhile.
5. `MSG_FREE` releases the message. A message belongs to the process that
   received it.
6. When `entry` returns, the program ends. Every remaining process is
   stopped, pending output is written, and the exit code is 0.

Build and run it:

```sh
gcc -std=gnu11 hello.c $(pkg-config --cflags --libs clpf) -o hello
./hello
```

```text
hello from #2
hello from #1
hello from #3
```

The order changes from run to run: the three greeters really run in parallel.

!!! tip "pkg-config path"
    If you installed to `~/.local`, run `export PKG_CONFIG_PATH=~/.local/lib/pkgconfig` first.

### With CMake

```cmake
cmake_minimum_required(VERSION 3.15)
project(hello C)
find_package(clpf 0.2 REQUIRED)
add_executable(hello hello.c)
target_link_libraries(hello clpf::clpf)
```

Then configure with `cmake -S . -B build -DCMAKE_PREFIX_PATH=~/.local`.

## Let it crash

Now add a process that fails and a supervisor that restarts it:

```c
#include <api/task.h>
#include <api/message.h>
#include <api/io.h>
#include <api/supervisor.h>

TASK(flaky, Pid boss) {
    PRINT("flaky started as pid %ld\n", self->pid);
    SEND(boss, NULL, 0, MAKE_STR_TAG("up"));
    EXIT_ERROR(); // something went wrong
}

TASK(entry, int argc, char *argv[]) {
    // Restart children forever, but at most 3 times per second
    Pid sup = SPAWN_SUPERVISOR("sup", SUP_ONE_FOR_ONE, 1000, 3);
    SUP_ADD_CHILD(sup, RES_PERMANENT, flaky, "flaky", 1, self->pid);

    for (int i = 0; i < 3; ++i) // the first start and two restarts
        MSG_FREE(RECEIVE(MAKE_STR_TAG("up")));
    PRINT("restarted twice, stopping\n");
}

CLPF_ENTRY(entry)
```

`flaky` crashes every time it starts, and each time the supervisor starts a
fresh copy with the same arguments. One more start may show up in the
output: the supervisor keeps working until the program has stopped. If it crashed more than 3 times within
1000 ms, the supervisor would give up: it would stop its children and exit
with an error, so that whoever supervises *it* can react. This is how
supervision trees contain failures.

## Next steps

- Read the [concepts](concepts.md) behind the model.
- Learn [selective receive and matchers](guides/messages.md).
- See the runnable programs in
  [`examples/`](https://github.com/AurelienBaraquin/CLPF/tree/main/examples),
  and the full showcase in
  [`main.c`](https://github.com/AurelienBaraquin/CLPF/blob/main/main.c):
  `./build/clpf_demo`.
