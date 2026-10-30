#pragma once

#include <clpf_internal.h>
#include <io_poller.h>

#define PRINT(fmt, ...) clpf_print(self, fmt, ##__VA_ARGS__)

#define FORMAT(fmt, ...) clpf_format(self, fmt, ##__VA_ARGS__)

// Waits for a line of stdin without blocking anything else; returns it with its
// newline, allocated in the caller's heap (FREE it), or NULL at the end of input
#define GETLINE() clpf_getline(self)

// Waits until fd is ready for events (IO_READ, IO_WRITE or both) without
// blocking anything else. Returns the IO_* flags that are ready, which may
// include IO_ERROR or IO_HANGUP, or -1 if fd cannot be watched (see IO_WATCH)
#define IO_WAIT(fd, events) clpf_io_wait_timeout(self, fd, events, CLPF_INFINITY)
// The same, giving up with 0 after timeout_ms
#define IO_WAIT_TIMEOUT(fd, events, timeout_ms) clpf_io_wait_timeout(self, fd, events, timeout_ms)

// Asks for one message tagged MAKE_FD_TAG(fd), carrying the ready IO_* flags
// as an int, when fd becomes ready. Returns -1 if fd is invalid, cannot be
// polled (regular files), or is watched by another process. The watch ends
// when it fires, with IO_UNWATCH, or when the process ends.
#define IO_WATCH(fd, events) clpf_io_watch(self, fd, events)
// Cancels a watch: 0, or -1 if there is none (it may have fired: then the
// message is, or is about to be, in the mailbox). Unwatch an fd before closing it.
#define IO_UNWATCH(fd) clpf_io_unwatch(self, fd)

int clpf_io_watch(Process *self, int fd, int events);
int clpf_io_unwatch(Process *self, int fd);
int clpf_io_wait_timeout(Process *self, int fd, int events, uint64_t timeout_ms);

char *clpf_vformat(Process *self, const char *fmt, va_list ap);
char *clpf_format(Process *self, const char *fmt, ...);
int clpf_print(Process *self, const char *fmt, ...);

char *clpf_getline(Process *self);
