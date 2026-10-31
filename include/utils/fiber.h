#pragma once

#include <stddef.h>

typedef struct process_s Process;

// ThreadSanitizer follows each process's call stack as a fiber of its own, so
// that switching coroutines, and moving processes between threads, keep its
// shadow stacks in step. Without ThreadSanitizer, these do nothing.
//
// The switches are inlined: a function of their own would enter on one fiber
// and return on the other, unbalancing both.

// Scheduler thread, before it runs processes: remembers its own fiber
void fiber_thread_init(void);
// Once its coroutine is gone (the process ends or hibernates)
void fiber_destroy(Process *process);

#if defined(__SANITIZE_THREAD__)
#include <sanitizer/tsan_interface.h>

// The fiber of the process, created at its first run
void **fiber_of(Process *process);
// The calling scheduler thread's own fiber
void *fiber_scheduler(void);

// Scheduler, right before resuming a process
#define fiber_enter(process)                      \
    do {                                          \
        void **clpf_fiber = fiber_of(process);    \
        if (!*clpf_fiber)                         \
            *clpf_fiber = __tsan_create_fiber(0); \
        __tsan_switch_to_fiber(*clpf_fiber, 0);   \
    } while (0)

// Process, right before giving the CPU back to its scheduler (yield or end)
#define fiber_leave() __tsan_switch_to_fiber(fiber_scheduler(), 0)

#else
#define fiber_enter(process) ((void)(process))
#define fiber_leave()        ((void)0)
#endif
