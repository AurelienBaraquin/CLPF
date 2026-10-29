#pragma once

#include <utils/pid.h>

// Reads stdin on a dedicated OS thread, so that a process waiting for input
// blocks neither its scheduler nor the I/O server. Each request is answered
// with one message to the requester: "stdin::line" carrying the line (without
// a terminating NUL), or "stdin::eof".

// Thread-safe; returns -1 if out of memory
int stdin_request_line(Pid requester);
