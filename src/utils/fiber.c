#include <utils/fiber.h>
#include <process.h>

#if defined(__SANITIZE_THREAD__)

static _Thread_local void *scheduler_fiber;

void fiber_thread_init(void) {
    scheduler_fiber = __tsan_get_current_fiber();
}

void **fiber_of(Process *process) {
    return &process->fiber;
}

void *fiber_scheduler(void) {
    return scheduler_fiber;
}

void fiber_destroy(Process *process) {
    if (process->fiber)
        __tsan_destroy_fiber(process->fiber);
    process->fiber = NULL;
}

#else

void fiber_thread_init(void) {
}

void fiber_destroy(Process *process) {
    (void)process;
}

#endif
