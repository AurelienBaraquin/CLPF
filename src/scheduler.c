#include <scheduler.h>

int scheduler_init(Scheduler* scheduler, Pid pid) {
    if (!scheduler)
        return -1;

    scheduler->pid = pid;
    atomic_init(&scheduler->process_count, 0);

    for (int i = 0; i < NB_RQ; i++) {
        mpscq_init(&scheduler->shared_rq[i]);
    }
    mpscq_init(&scheduler->wakeups);
    idle_init(&scheduler->idle);
    for (int i = 0; i < NB_RQ; i++) {
        runq_init(&scheduler->rq[i]);
    }

    scheduler->system_thread_id = -1;

    scheduler->scheduler_acoro = NULL;

    return 0;
}

void scheduler_destroy(Scheduler* scheduler) {
    if (!scheduler)
        return;

    for (int i = 0; i < NB_RQ; i++) {
        runq_clear(&scheduler->rq[i], NULL);
    }

    if (scheduler->scheduler_acoro)
        aco_destroy(scheduler->scheduler_acoro);

    idle_destroy(&scheduler->idle);
}

void scheduler_push_shared(Scheduler* scheduler, Process* process) {
    if (!scheduler || !process)
        return;

    switch (process->priority) {
        case PROCESS_PRIORITY_LOW:
        case PROCESS_PRIORITY_MEDIUM:
            mpscq_push(&scheduler->shared_rq[2], &process->shared_node);
            break;
        case PROCESS_PRIORITY_HIGH:
            mpscq_push(&scheduler->shared_rq[1], &process->shared_node);
            break;
        case PROCESS_PRIORITY_VERY_HIGH:
            mpscq_push(&scheduler->shared_rq[0], &process->shared_node);
            break;
        default:
            break;
    }

    scheduler_wake(scheduler);
}

void scheduler_wake(Scheduler* scheduler) {
    idle_wake(&scheduler->idle);
}

void scheduler_push(Scheduler* scheduler, Process* process) {
    if (!scheduler || !process)
        return;

    switch (process->priority) {
        case PROCESS_PRIORITY_LOW:
        case PROCESS_PRIORITY_MEDIUM:
            runq_push(&scheduler->rq[2], process);
            break;
        case PROCESS_PRIORITY_HIGH:
            runq_push(&scheduler->rq[1], process);
            break;
        case PROCESS_PRIORITY_VERY_HIGH:
            runq_push(&scheduler->rq[0], process);
            break;
        default:
            break;
    }
}

void scheduler_drain(Scheduler* scheduler) {
    if (!scheduler)
        return;

    for (int i = 0; i < NB_RQ; i++) {
        MPSCQNode* node = mpscq_drain(&scheduler->shared_rq[i]);
        while (node) {
            Process* process = container_of(node, Process, shared_node);
            node = node->next;
            runq_push(&scheduler->rq[i], process);
        }
    }
}

void scheduler_drain_wakeups(Scheduler* scheduler) {
    MPSCQNode* node = mpscq_drain(&scheduler->wakeups);
    while (node) {
        Process* process = container_of(node, Process, wake_node);
        node = node->next;

        // Cleared before looking at the state: a later delivery queues the process again
        atomic_store(&process->wake_pending, false);
        if (process->state == PROCESS_WAITING) {
            process->state = PROCESS_READY;
            scheduler_push(scheduler, process);
        }
    }
}

Process* pick_process(Scheduler* scheduler) {
    if (!scheduler)
        return NULL;

    scheduler_drain_wakeups(scheduler);
    scheduler_drain(scheduler);

    for (int i = 0; i < NB_RQ; ++i) {
        RunQueue* rq = &scheduler->rq[i];

        size_t n = runq_size(rq);
        for (size_t j = 0; j < n; ++j) {
            Process* process = runq_pop(rq);

            // Only low priority processes are ever delayed
            if (process->schedule_count > 0) {
                process->schedule_count--;
                runq_push(rq, process);
                continue;
            }

            if (process->priority == PROCESS_PRIORITY_LOW)
                process->schedule_count = LOW_PRIORITY_SKIPS;

            return process;
        }
    }

    return NULL;
}
