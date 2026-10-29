#include <scheduler.h>

int scheduler_init(Scheduler* scheduler, Pid pid) {
    if (!scheduler)
        return -1;

    scheduler->pid = pid;
    scheduler->immigrate_from = NULL;
    scheduler->emigrate_to = NULL;
    atomic_init(&scheduler->process_count, 0);

    for (int i = 0; i < NB_RQ; i++) {
        mpscq_init(&scheduler->shared_rq[i]);
    }
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
        mpscq_clear(&scheduler->shared_rq[i], NULL);
    }
    for (int i = 0; i < NB_RQ; i++) {
        runq_clear(&scheduler->rq[i], NULL);
    }

    if (scheduler->scheduler_acoro)
        aco_destroy(scheduler->scheduler_acoro);
}

void scheduler_push_shared(Scheduler* scheduler, Process* process) {
    if (!scheduler || !process)
        return;

    switch (process->priority) {
        case PROCESS_PRIORITY_LOW:
        case PROCESS_PRIORITY_MEDIUM:
            mpscq_push(&scheduler->shared_rq[2], process);
            break;
        case PROCESS_PRIORITY_HIGH:
            mpscq_push(&scheduler->shared_rq[1], process);
            break;
        case PROCESS_PRIORITY_VERY_HIGH:
            mpscq_push(&scheduler->shared_rq[0], process);
            break;
        default:
            break;
    }
}

void scheduler_push(Scheduler* scheduler, Process* process) {
    if (!scheduler || !process)
        return;
    
    if (process->state == PROCESS_WAITING) {
        runq_push(&scheduler->rq[3], process);
        return;
    }

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
            Process* process = (Process*)node->data;
            runq_push(&scheduler->rq[i], process);
            MPSCQNode* next = node->next;
            free(node);
            node = next;
        }
    }
}

void update_waiting_processes(Scheduler* scheduler) {
    if (!scheduler) return;

    RunQueue temp_rq = {0};
    runq_init(&temp_rq);


    Process* process;
    while ((process = runq_pop(&scheduler->rq[3])) != NULL) {
        if (process->state == PROCESS_WAITING) {
            runq_push(&temp_rq, process);
        } else {
            process->state = PROCESS_READY;
            scheduler_push(scheduler, process);
        }
    }

    while ((process = runq_pop(&temp_rq)) != NULL) {
        runq_push(&scheduler->rq[3], process);
    }
}

Process* pick_process(Scheduler* scheduler) {
    if (!scheduler)
        return NULL;

    update_waiting_processes(scheduler);
    scheduler_drain(scheduler);

    for (int i = 0; i < NB_RQ - 1; ++i) {
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
