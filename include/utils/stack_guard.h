#pragma once

typedef struct process_s Process;

// Reports stack overflows of processes: each process stack starts with a guard
// page, and a fault on it is reported with the process name before crashing.

// Installs the SIGSEGV handler (once, before the scheduler threads start)
void stack_guard_install(void);
// Gives the calling scheduler thread an alternate signal stack to run the handler on
void stack_guard_thread_init(void);
// Records the process running on this thread (NULL between two processes)
void stack_guard_set_running(Process *process);
// The process running on this thread, or NULL
Process *stack_guard_running(void);
