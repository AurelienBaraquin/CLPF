#include <scheduler.h>

int scheduler_init(Scheduler* scheduler, Pid pid) {
    if (!scheduler)
        return -1;

    scheduler->pid = pid;
    scheduler->immigrate_from = NULL;
    scheduler->emigrate_to = NULL;
    atomic_init(&scheduler->process_count, 0);

    mpscq_init(&scheduler->ready_vh_queue);
    mpscq_init(&scheduler->ready_h_queue);
    mpscq_init(&scheduler->ready_m_and_l_queue);
    mpscq_init(&scheduler->waiting_queue);

    return 0;
}

void scheduler_destroy(Scheduler* scheduler) {
    if (!scheduler)
        return;

    mpscq_clear(&scheduler->ready_vh_queue, NULL);
    mpscq_clear(&scheduler->ready_h_queue, NULL);
    mpscq_clear(&scheduler->ready_m_and_l_queue, NULL);
    mpscq_clear(&scheduler->waiting_queue, NULL);
}

void scheduler_loop(Scheduler* scheduler) {
    if (!scheduler)
        return;

}
