#pragma once

#include <stddef.h>

// Hands memory releases (munmap) over to one thread, so that the threads
// running processes do not pay for them: unmapping flushes the TLB of every
// core. Any thread defers, the reaper thread runs.

// Called, from the deferring thread, when the first block of a batch waits
void reaper_set_wake(void (*wake)(void));
// Queues release(block, size): 0, or -1 if out of memory (release it yourself then)
int reaper_defer(void (*release)(void *block, size_t size), void *block, size_t size);
// Reaper thread: releases every queued block
void reaper_run(void);
