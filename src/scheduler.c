#include <scheduler.h>

int scheduler_init(Scheduler* scheduler, size_t pid) {
    if (!scheduler)
        return -1;

    scheduler->pid = pid;
    scheduler->immigrate_from = NULL;
    scheduler->emigrate_to = NULL;

    TAILQ_INIT(&scheduler->ready_vh_queue);
    TAILQ_INIT(&scheduler->ready_h_queue);
    TAILQ_INIT(&scheduler->ready_m_and_l_queue);
    TAILQ_INIT(&scheduler->waiting_queue);

    return 0;
}

void scheduler_destroy(Scheduler* scheduler) {
    if (!scheduler)
        return;

    Process* process;
    while (!TAILQ_EMPTY(&scheduler->ready_vh_queue)) {
        process = TAILQ_FIRST(&scheduler->ready_vh_queue);
        TAILQ_REMOVE(&scheduler->ready_vh_queue, process, next);
        process_destroy(process);
    }
    while (!TAILQ_EMPTY(&scheduler->ready_h_queue)) {
        process = TAILQ_FIRST(&scheduler->ready_h_queue);
        TAILQ_REMOVE(&scheduler->ready_h_queue, process, next);
        process_destroy(process);
    }
    while (!TAILQ_EMPTY(&scheduler->ready_m_and_l_queue)) {
        process = TAILQ_FIRST(&scheduler->ready_m_and_l_queue);
        TAILQ_REMOVE(&scheduler->ready_m_and_l_queue, process, next);
        process_destroy(process);
    }
    while (!TAILQ_EMPTY(&scheduler->waiting_queue)) {
        process = TAILQ_FIRST(&scheduler->waiting_queue);
        TAILQ_REMOVE(&scheduler->waiting_queue, process, next);
        process_destroy(process);
    }
}

void scheduler_loop(Scheduler* scheduler) {
    if (!scheduler)
        return;

}
