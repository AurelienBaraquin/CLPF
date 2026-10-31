# Troubleshooting and FAQ

## Common mistakes

### My program exits before my processes finish

The program ends when the **entry process** ends, and every other process is
then stopped. Wait for what you need before returning from the entry task:
a result message, a DOWN (`MONITOR` the workers), or a notification.

### The process memory keeps growing

Every received `Message` must be released with `MSG_FREE`, and every `ALLOC`,
`FORMAT` and `GETLINE` result with `FREE`. Memory is reclaimed when a process
ends, so leaks only show in long-running processes, typically servers.

A mailbox full of messages that nothing receives grows too: receive with
`NO_TAG` in your main loop, or drain unexpected messages.

### The receiver reads garbage

A payload is copied byte by byte. Pointers inside it point into the sender's
memory, which the receiver must not use. Send the values themselves, and
include the terminating NUL when you send a string
(`strlen(text) + 1`).

### Everything freezes while one process computes

The scheduler is cooperative: a loop that never calls the runtime keeps its
thread, and every process on that scheduler waits behind it. Call `REDUCE(n)`
in long loops (see [scheduling](guides/scheduling.md)).

### Finding the process that blocks everyone

The [watchdog](guides/observability.md#the-watchdog) names a process that
runs too long without yielding. To see every process at once, send
`SIGUSR1` to the program: it writes a [dump](guides/observability.md#dumping-everything)
to stderr, with each process's state and queued messages.

### Everything freezes during I/O

A blocking system call (`read`, `accept`, `recv`, `sleep`...) blocks the
scheduler thread, not only the process. Use `SLEEP`, `GETLINE` and
`RECEIVE_TIMEOUT`, which are process-aware. Put sockets and pipes in
non-blocking mode and wait for them with
[`IO_WAIT`](guides/io.md#sockets-pipes-and-other-file-descriptors). For
blocking work without a file descriptor, use a dedicated thread that sends
results with `clpf_send(NULL, ...)`.

### `REQUEST` never returns

The server never replied with the **same tag**, or it died. Answer with
`REPLY(msg, ...)`, and prefer `REQUEST_REF(pid, data, size, timeout_ms)`,
which bounds the wait. If the server may die, see
[calling a server that may die](patterns.md#calling-a-server-that-may-die).

### A pid I stored stopped working after a restart

A restarted process gets a new pid. Register long-lived services and look
them up with `WHEREIS` at each use.

### `clpf: stack overflow in process 'x'`

The process needs a larger stack: spawn it with
`SPAWN_OPTS(func, .stack_size = ...)`, or avoid large local arrays.

### `SPAWN` arguments are wrong in the child

`SPAWN` arguments are machine words: integers and pointers, at most 5. A
`double` or a struct does not work; use `SPAWN_WITH`. A pointer to a local
variable dangles once the spawner returns.

## Build problems

### `expected ')' before 'int'` on `TASK(...)`

Include the header that defines the macros, `api/task.h` or `api/base.h`.

### Warnings about braced groups within expressions

The API macros use GNU C statement expressions, which `-pedantic` reports.
Compile with `-std=gnu11` (or `-std=c11` without `-pedantic`); GCC and
Clang both support them. Compilers without GNU extensions are not supported.

### ThreadSanitizer: `unexpected memory mapping`

Recent kernels randomise memory more than TSan supports. Run with
`setarch -R` (`setarch -R ./program`, `setarch -R ctest ...`) or set
`sudo sysctl vm.mmap_rnd_bits=28`.

### Undefined references to `aco_*`, `tlsf_*` or `stbsp_*`

Link the vendored libraries too: `-lclpf -lclpf_tlsf -lclpf_aco
-lclpf_stb_sprintf -lpthread`, or use pkg-config or the CMake package,
which do it for you.

## FAQ

### Is this Erlang?

No. It is the Erlang *programming model*: lightweight isolated processes,
copied messages, selective receive, monitors, links and supervisors, as a C
library. There is no BEAM, no distribution over the network and no hot code
reloading.

### How many processes can I have?

65 535 at the same time. Each costs at least about 32 KiB of address space
(stack and heap) and a few microseconds to start. Blocked processes cost no
CPU.

### Is a process isolated like an OS process?

Its heap, stack and mailbox are private by design, and the API never
gives you another process's memory. But all processes share one address
space: a wild pointer in C can still corrupt anything. CLPF isolates failures
that are *detected* (`EXIT_ERROR`, a kill, a crash of a linked process), not
memory corruption.

### Can I use threads alongside CLPF?

Yes. Your own threads can send messages to processes with
`clpf_send(NULL, pid, data, size, tag)` and look up names with
`clpf_whereis`. They must not call the macros, which need a `self`.

### Can I call libraries from a process?

Yes, as long as they don't block for long (see the I/O point above) and
don't need much stack. Thread-local state in a library is risky, since a
process can move to another thread between two calls.

### Is message ordering guaranteed?

Between one sender and one receiver, yes. Between different senders, no.

### What are the limits?

- x86-64 Linux only: libaco's context switch and the argument trampoline are
  x86-64 assembly.
- Preemption only happens in runtime calls and `REDUCE`.
- A process name is at most 63 characters.
- Experimental: the API may still change.
