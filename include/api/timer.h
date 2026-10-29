#pragma once

#include <api/message.h>
#include <timer.h>

// Sends a copy of the message to target after delay_ms, as if self sent it.
// Returns a TimerId (never 0) for CANCEL_TIMER, or 0 if out of memory.
#define SEND_AFTER(target, delay_ms, data, size, tag) timer_add(self->pid, target, delay_ms, data, size, tag)

// Returns 0, or -1 if the timer already fired (its message was sent) or was cancelled
#define CANCEL_TIMER(id) timer_cancel(id)

// RECEIVE that gives up after timeout_ms and returns NULL (0 only polls, like FETCH)
#define RECEIVE_TIMEOUT(tag, timeout_ms) clpf_receive_timeout(self, TM_SAME, tag, timeout_ms)

#define RECEIVE_IF_TIMEOUT(match, pattern, timeout_ms) clpf_receive_timeout(self, match, pattern, timeout_ms)

// Waits without blocking the scheduler; messages arriving meanwhile stay in the mailbox
#define SLEEP(ms) clpf_sleep(self, ms)
