# Input and output

Header: `api/io.h`

| Macro | Meaning |
|---|---|
| `PRINT(fmt, ...)` | Formats like `printf` and sends the text to the I/O server. Returns `0`, or `-1` if the text could not be formatted |
| `FORMAT(fmt, ...)` | Formats into a string allocated in the caller's heap (`FREE` it), or `NULL` |
| `GETLINE()` | Waits for a line of stdin; returns it with its newline, allocated in the caller's heap (`FREE` it), or `NULL` at the end of input |

## PRINT

Output goes through a single process, the I/O server (`server::io`):

- one `PRINT` is written in one piece: lines from different processes
  never interleave;
- the output of one process stays in order;
- `PRINT` never blocks the caller on the terminal or on a pipe;
- when the program ends, everything already printed is written before exit.

`printf` still works, but its output can interleave with other processes',
and it is buffered by the C library. Use `PRINT` in processes, and `fprintf(stderr, ...)`
for low-level diagnostics.

`PRINT` formats with stb_sprintf: the usual conversions (`%d %ld %u %x %s %f %p %zu`...)
are supported.

## GETLINE

`GETLINE` hands the read to a dedicated OS thread and waits for its answer
as a message. While it waits, only the calling process is blocked: its
scheduler runs other processes, and `PRINT` keeps working.

```c
#include <api/task.h>
#include <api/io.h>
#include <string.h>

TASK(entry, int argc, char *argv[]) {
    PRINT("Type lines, Ctrl-D to stop\n");
    char *line;
    int count = 0;
    while ((line = GETLINE()) != NULL) {
        PRINT("%zu characters\n", strlen(line));
        FREE(line);
        ++count;
    }
    PRINT("%d lines\n", count);
}

CLPF_ENTRY(entry)
```

Lines are read in the order the requests arrive, so two processes calling
`GETLINE` at the same time each get a different line.

## Sockets, pipes and other file descriptors

| Macro | Meaning |
|---|---|
| `IO_WAIT(fd, events)` | Waits until `fd` is ready for `events` (`IO_READ`, `IO_WRITE` or both). Returns the ready flags, or `-1` if `fd` cannot be watched |
| `IO_WAIT_TIMEOUT(fd, events, ms)` | The same, giving up with `0` after `ms` |
| `IO_WATCH(fd, events)` | Asks for one message tagged `MAKE_FD_TAG(fd)` when `fd` is ready; its payload is an `int` of ready flags. `0`, or `-1` |
| `IO_UNWATCH(fd)` | Cancels a watch: `0`, or `-1` if there is none (it may have fired: the message is then in the mailbox, or about to be) |

The ready flags are `IO_READ`, `IO_WRITE`, and, reported only, `IO_ERROR`
and `IO_HANGUP` (the peer closed its end).

A blocking system call (`read`, `accept`, `connect`...) blocks the **whole
scheduler thread** that runs the process, and every process waiting on it. So
put file descriptors in non-blocking mode, and wait for them with `IO_WAIT`:
it blocks only the calling process. A dedicated OS thread watches every fd
with epoll and wakes the process with a message.

The usual shape is one process per connection, written as plain sequential
code:

```c
#define _GNU_SOURCE // accept4
#include <api/task.h>
#include <api/message.h>
#include <api/io.h>
#include <api/log.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

// One process per connection: it waits for data without blocking anyone
TASK(connection, int fd) {
    char buffer[256];
    while (IO_WAIT(fd, IO_READ) > 0) {
        ssize_t got = read(fd, buffer, sizeof buffer);
        if (got <= 0 || write(fd, buffer, (size_t)got) != got)
            break; // closed by the peer, or an error
    }
    close(fd);
}

TASK(acceptor, int listener) {
    while (IO_WAIT(listener, IO_READ) > 0) {
        int fd = accept4(listener, NULL, NULL, SOCK_NONBLOCK);
        if (fd >= 0)
            SPAWN(connection, 1, fd);
    }
}

TASK(entry, int argc, char *argv[]) {
    // Listen on a port the system picks
    int listener = socket(AF_INET, SOCK_STREAM | SOCK_NONBLOCK, 0);
    struct sockaddr_in address = {.sin_family = AF_INET, .sin_addr.s_addr = htonl(INADDR_LOOPBACK)};
    socklen_t length = sizeof address;
    if (bind(listener, (struct sockaddr *)&address, length) != 0 || listen(listener, 16) != 0)
        EXIT_ERROR();
    getsockname(listener, (struct sockaddr *)&address, &length);
    SPAWN(acceptor, 1, listener);
    LOG_INFO("listening on port %d", ntohs(address.sin_port));

    // A client, in the same program for the example
    int client = socket(AF_INET, SOCK_STREAM | SOCK_NONBLOCK, 0);
    connect(client, (struct sockaddr *)&address, length); // in progress
    if (IO_WAIT(client, IO_WRITE) != IO_WRITE || write(client, "hello", 5) != 5)
        EXIT_ERROR();

    char reply[16] = {0};
    if (IO_WAIT(client, IO_READ) == IO_READ && read(client, reply, sizeof reply - 1) > 0)
        PRINT("echo: %s\n", reply);
    close(client);
}

CLPF_ENTRY(entry)
```

Rules:

- A watch fires **once**. Wait again for the next event.
- One process watches a given fd at a time; `IO_WATCH` by another process
  returns `-1` until the watch ends. Watching again an fd you already watch
  replaces the request.
- Watches end when they fire, with `IO_UNWATCH`, or when the process ends.
  Call `IO_UNWATCH` before closing an fd you still watch.
- Regular files cannot be watched (epoll refuses them: they are always
  ready). Read them directly, or from a dedicated thread if they are slow.
- `IO_WATCH` lets a process wait for several things at once: watch a socket,
  then `RECEIVE` with a matcher that accepts both the fd message and your
  other messages.

For other blocking work that has no file descriptor, use the pattern CLPF
uses for stdin: a dedicated OS thread that performs the call and sends the
result as a message with `clpf_send(NULL, pid, data, size, tag)`, which may
be called from any thread.

## Logging

Header: `api/log.h`

| Macro | Meaning |
|---|---|
| `LOG_DEBUG(fmt, ...)`, `LOG_INFO`, `LOG_WARN`, `LOG_ERROR` | Writes one line to stderr, unless the level is below the current one. Returns `0`, or `-1` if out of memory |
| `clpf_set_log_level(level)` | Sets the level for all processes: `LOG_LEVEL_DEBUG`, `_INFO`, `_WARN`, `_ERROR` or `_NONE` |
| `clpf_log_level()` | The current level |

Each line carries the time, the level, the process name and pid:

```text
14:03:27.512 WARN  disk_watch<12>: disk at 91%
```

No newline is needed in the format. Lines go through the I/O server, like
`PRINT`, so they never mix with each other, and stay in order for a process.
The level starts from the `CLPF_LOG_LEVEL` environment variable (`debug`,
`info`, `warn`, `error` or `none`, in any case), `info` by default:

```bash
CLPF_LOG_LEVEL=debug ./my_program
```
