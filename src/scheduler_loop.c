#include <scheduler.h>
#include <flags_internal.h>
#include <utils/ox.h>
#include <stdio.h>
#include <api/base.h>
#include <assert.h>
#include <utils/stack_guard.h>
#include <utils/spinlock.h>
#include <utils/time.h>
#include <watchdog.h>
#include <utils/fiber.h>

void co_entrypoint() {
    Process *proc = (Process *)aco_get_arg();

    if (!proc)
        return;

    funcall_call(&proc->taskcall);

    // The task returned without calling EXIT: that is a normal exit
    exiting(proc, EXIT_REASON_NORMAL);
}

// The process's coroutine ended to hibernate: drop it and its stack, trim the
// heap, and park the process until a message comes (any message)
static process_state hibernate(Process *process) {
    process->hibernating = false;
    aco_destroy(process->acoro);
    process->acoro = NULL;
    fiber_destroy(process);
    process_release_stack(process);
    safe_heap_trim(&process->heap);

    pthread_mutex_lock(&process->lock);
    bool ready = process->mailbox.inbox_head || process->mailbox.seen_head || atomic_load(&process->kill_pending);
    process->wait_match = NULL;
    process->state = ready ? PROCESS_READY : PROCESS_WAITING;
    pthread_mutex_unlock(&process->lock);
    return process->state;
}

process_state execute_process(Scheduler *scheduler, Process *process) {
    if (!process)
        return PROCESS_EXITING;

    // First run: the coroutine is created on the scheduler that runs it
    if (!process->acoro) {
        process->share_stack = stack_acquire(process->stack_size);
        process->acoro = aco_create(scheduler->scheduler_acoro, process->share_stack, 0, &co_entrypoint, process);
        aco_assertptr(process->acoro);
    }

    if (process->acoro->is_end == 1) {
        process->state = PROCESS_EXITING;
        return process->state;
    }

    process->state = PROCESS_RUNNING;
    atomic_fetch_add_explicit(&process->runs, 1, memory_order_relaxed);
    assert(process->acoro);
    assert(process->acoro->is_end == 0);
    stack_guard_set_running(process);
    watchdog_enter(scheduler, process);
    fiber_enter(process);
    aco_resume(process->acoro);
    watchdog_leave(scheduler);
    stack_guard_set_running(NULL);

    if (UNLIKELY(process->hibernating))
        return hibernate(process);

    return process->state;
}

static bool scheduler_has_work(void *arg) {
    Scheduler *scheduler = arg;

    if (exit_in_progress || !mpscq_empty(&scheduler->wakeups))
        return true;
    // A timer armed from another thread, earlier than the planned wakeup
    uint64_t next_timer = atomic_load(&scheduler->timers.next_deadline);
    if (next_timer && (scheduler->sleep_deadline == 0 || next_timer < scheduler->sleep_deadline))
        return true;
    for (int i = 0; i < NB_RQ; i++) {
        if (!mpscq_empty(&scheduler->shared_rq[i]))
            return true;
    }
    return false;
}

void scheduler_loop(Scheduler *scheduler) {
    if (!scheduler)
        return;

    aco_thread_init(NULL);
    stack_guard_thread_init();
    scheduler->scheduler_acoro = aco_create(NULL, NULL, 0, NULL, NULL);
    timer_queue_set_current(&scheduler->timers);
    fiber_thread_init();
    clpf_slot_cache_attach(&scheduler->slot_cache);

    while (1) {
        if (UNLIKELY(exit_in_progress)) {
            clpf_wake_all();
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

        // Due timers first: their messages may make processes ready
        uint64_t next_timer = atomic_load(&scheduler->timers.next_deadline);
        if (next_timer) {
            uint64_t now = now_ms();
            if (now >= next_timer)
                timer_queue_fire_due(&scheduler->timers, now);
        }

        // 8. Pick a process to execute
        Process *process = pick_process(scheduler);

        if (!process) {
            atomic_store(&scheduler->searching, true);
            bool work_elsewhere = scheduler_request_work(scheduler);

            // Replies often come back within microseconds: spin a little before
            // paying for a sleep and a wakeup
            for (int spin = 0; spin < SCHEDULER_IDLE_SPINS && !scheduler_has_work(scheduler); ++spin)
                cpu_relax();

            // Overloaded schedulers hand work over when they next pick: ask again soon.
            // Sleep until the next timer at most.
            uint64_t deadline = work_elsewhere ? now_ms() + SCHEDULER_STEAL_RETRY_MS : 0;
            uint64_t timer_deadline = atomic_load(&scheduler->timers.next_deadline);
            if (timer_deadline && (!deadline || timer_deadline < deadline))
                deadline = timer_deadline;
            scheduler->sleep_deadline = deadline;
            atomic_store(&scheduler->searching, false);
            idle_wait_until(&scheduler->idle, scheduler_has_work, scheduler, deadline);
            continue;
        }

        // 10. Execute the process, unless it was killed: then it ends without being resumed
        if (UNLIKELY(atomic_load(&process->kill_pending))) {
            process_terminate(process, process->kill_reason);
            process->state = PROCESS_EXITING;
        } else {
            process->state = execute_process(scheduler, process);
        }

        // 11. Handle the process state
        switch (process->state) {
            case PROCESS_READY:
            case PROCESS_RUNNING:
                process->state = PROCESS_READY;
                scheduler_push(scheduler, process);
                break;
            case PROCESS_WAITING:
                // Parked: it comes back through the wakeup queue
                break;
            case PROCESS_EXITING:
                // exiting() stopped further wakeups; take its pending one out of
                // the queue before the slot is freed or reused
                scheduler_drain_wakeups(scheduler);
                clpf_process_end(process);
                atomic_fetch_sub(&scheduler->process_count, 1);
                // The main thread drives the shutdown: it waits for these ends
                if (atomic_load(&clpf.shutdown_requested))
                    clpf_wake_main();
                break;
            default: break;
        }
    }
}
