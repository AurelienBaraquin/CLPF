#pragma once

#include <process.h>
#include <pthread.h>

typedef enum scheduler_migration_state_e {
    SCHEDULER_IMMIGRATING,
    SCHEDULER_EMIGRATING,
    SCHEDULER_BALANCED,
} scheduler_migration_state;

typedef struct scheduler_s {
    pthread_t system_thread_id;
    Pid pid;

    struct scheduler_s* immigrate_from;
    struct scheduler_s* emigrate_to;

    MPSCQ ready_vh_queue;
    MPSCQ ready_h_queue;
    MPSCQ ready_m_and_l_queue;

    MPSCQ waiting_queue;
} Scheduler;

int scheduler_init(Scheduler* scheduler, Pid pid);
void scheduler_destroy(Scheduler* scheduler);
void scheduler_loop(Scheduler* scheduler);
