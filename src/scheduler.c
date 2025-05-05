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

    scheduler->system_thread_id = pthread_self();

    pthread_mutex_init(&scheduler->mutex, NULL);
    pthread_cond_init(&scheduler->cond, NULL);

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

    pthread_mutex_destroy(&scheduler->mutex);
    pthread_cond_destroy(&scheduler->cond);
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

    // Notify the scheduler thread
    pthread_mutex_lock(&scheduler->mutex);
    pthread_cond_signal(&scheduler->cond);
    pthread_mutex_unlock(&scheduler->mutex);
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

Process* get_process_by_priority(Scheduler* scheduler) {
    if (!scheduler)
        return NULL;

    Process* process = NULL;

    for (int i = 0; i < NB_RQ - 1; i++) { // -1 to avoid checking rq[3] (waiting)
        if (!runq_empty(&scheduler->rq[i])) {
            process = runq_pop(&scheduler->rq[i]);
            break;
        }
    }

    return process;
}

Process* pick_process(Scheduler* scheduler) {
    if (!scheduler)
        return NULL;

    Process* process = get_process_by_priority(scheduler);

    if (!process) {
        scheduler_drain(scheduler);
        process = get_process_by_priority(scheduler);
    }

    if (!process) {
        return NULL;
    }

    process->schedule_count--;

    if (process->schedule_count == 0) {
        return process;
    }

    scheduler_push(scheduler, process);

    return pick_process(scheduler);
}
