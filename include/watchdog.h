#pragma once

#include <stdint.h>

typedef struct scheduler_s Scheduler;
typedef struct process_s Process;

// Reports, once, a process that runs longer than CLPF_WATCHDOG_MS (1000 by
// default, 0 disables it) without giving the hand back. The main thread only
// wakes up for it while processes run.

// Reads CLPF_WATCHDOG_MS, before the schedulers start
void watchdog_init(void);
// Around each resume of a process (scheduler thread)
void watchdog_enter(Scheduler *scheduler, Process *process);
void watchdog_leave(Scheduler *scheduler);
// Main thread: reports long runners; returns when to check again (0: once a process runs)
uint64_t watchdog_check(uint64_t now);
