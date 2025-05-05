#include <scheduler.h>
#include <flags_internal.h>
#include <utils/ox.h>
#include <stdio.h>

__thread Process *__proc;
__thread cothread_t *__sch_co;

void co_entrypoint() {
    if (!__proc || !__sch_co)
        return;

    Process* proc = __proc;
    cothread_t* sch_co = __sch_co;

    funcall_call(&proc->taskcall);
    co_switch(*sch_co);
}

void scheduler_loop(Scheduler* scheduler) {
    if (!scheduler)
        return;

    cothread_t sch_co = co_active();

    while (1) {
        #ifdef RUNNING_ON_VALGRIND
        usleep(1000); // Sleep for 1ms to avoid busy waiting
        #else
        LOCK_UNTIL(runq_empty(&scheduler->rq[0]) ||
                   runq_empty(&scheduler->rq[1]) ||
                   runq_empty(&scheduler->rq[2]) ||
                   !exit_in_progress,
                   &scheduler->cond, &scheduler->mutex);
        #endif

        if (UNLIKELY(exit_in_progress)) {
            break;
        }
        // Process scheduling logic goes here

        // 0. Pull queues
        // 1. Update reduction counters
        // 2. Check timers
        // 3. If needed check balance
        // 4. If needed migrate processes and ports
        // 5. Do auxiliary scheduler work
        // 6. If needed check IO and update time
        // 7. While needed pick a port task to execute
        // 8. Pick a process to execute
        // 9. Handle signals of the process (exemple : EXIT, DOWN, MONITOR, UNLINK ect...)
        // 10. Execute the process

        Process* process = pick_process(scheduler);

        if (process) {
            __proc = process;
            __sch_co = &sch_co;
            process->state = PROCESS_RUNNING;
            co_switch(process->coro);
            process->state = PROCESS_EXITING;
            atomic_fetch_sub(&scheduler->process_count, 1);
        }
    }
}
