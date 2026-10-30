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

## Other I/O

Blocking system calls (`read`, `accept`, `sleep`...) block the **whole
scheduler thread** that runs the process, and every other process waiting on
it. Keep them out of processes, or confine them the way CLPF does for stdin:
a dedicated OS thread that performs the call and sends the result as a
message with `clpf_send(NULL, pid, data, size, tag)`, which may be called from
any thread.
