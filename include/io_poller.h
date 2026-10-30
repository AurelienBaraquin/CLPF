#pragma once

#include <stdint.h>
#include <utils/pid.h>

// Watches file descriptors on a dedicated OS thread with epoll, so that a
// process waiting for I/O blocks neither its scheduler nor anyone else. A
// watch fires once: its owner gets one message tagged MAKE_FD_TAG(fd), from
// INVALID_PID, whose payload is an int of IO_* flags, and the watch is gone.

// Readiness flags, in requests and in the ready message
#define IO_READ   0x1
#define IO_WRITE  0x2
#define IO_ERROR  0x4 // Only reported
#define IO_HANGUP 0x8 // Only reported

// Thread-safe; returns -1 if fd is invalid, cannot be polled (regular files),
// is already watched by another process, or the poller could not start.
// Watching again an fd one already watches replaces the request.
int io_poller_watch(Pid pid, int fd, int events);
// Returns -1 if pid does not watch fd (it may just have fired)
int io_poller_unwatch(Pid pid, int fd);
// Drops every watch of a process that ended (main thread)
void io_poller_forget(Pid pid);
