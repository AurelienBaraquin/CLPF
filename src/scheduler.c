#include <scheduler.h>

int scheduler_init(Scheduler* scheduler, size_t pid) {
    if (!scheduler)
        return -1;

    scheduler->pid = pid;
    scheduler->immigrate_from = NULL;
    scheduler->emigrate_to = NULL;

    mpscq_init(&scheduler->ready_vh_queue);
    mpscq_init(&scheduler->ready_h_queue);
    mpscq_init(&scheduler->ready_m_and_l_queue);
    mpscq_init(&scheduler->waiting_queue);

    return 0;
}

void scheduler_destroy(Scheduler* scheduler) {
    if (!scheduler)
        return;

    Process *proc = NULL;

    proc = mpscq_drain(&scheduler->ready_vh_queue);
    while (proc) {
        process_destroy(proc);
        proc = proc->next;
    }

    proc = mpscq_drain(&scheduler->ready_h_queue);
    while (proc) {
        process_destroy(proc);
        proc = proc->next;
    }

    proc = mpscq_drain(&scheduler->ready_m_and_l_queue);
    while (proc) {
        process_destroy(proc);
        proc = proc->next;
    }

    proc = mpscq_drain(&scheduler->waiting_queue);
    while (proc) {
        process_destroy(proc);
        proc = proc->next;
    }
}

void scheduler_loop(Scheduler* scheduler) {
    if (!scheduler)
        return;

}
