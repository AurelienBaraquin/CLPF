#pragma once

#include <mailbox.h>
#include <stdint.h>

// One-shot timers run by the main thread: when one is due, its message is
// delivered as if `from` had sent it. Thread-safe.
typedef uint64_t TimerId; // never 0

void timers_init(void);
void timers_destroy(void);

// Copies data and the tag string; returns the timer id, or 0 if out of memory
TimerId timer_add(Pid from, Pid to, uint64_t delay_ms, const void *data, size_t size, Tag tag);
// Fires every interval_ms (> 0, otherwise returns 0) until cancelled or until `to` is gone
TimerId timer_add_interval(Pid from, Pid to, uint64_t interval_ms, const void *data, size_t size, Tag tag);
// A timer whose message is MAKE_TIMEOUT_TAG(id), empty, sent to pid itself
TimerId timer_add_timeout(Pid pid, uint64_t delay_ms);
// Returns 0, or -1 if the timer already fired or was cancelled.
// A message of a timer that fired is in the receiver's inbox before this returns.
int timer_cancel(TimerId id);

// Main thread: delivers every due timer, returns the deadline of the next one (now_ms() clock), 0 if none
uint64_t timers_fire_due(void);
