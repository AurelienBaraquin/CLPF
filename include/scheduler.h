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
    size_t pid;

    struct scheduler_s* immigrate_from;
    struct scheduler_s* emigrate_to;

    TAILQ_HEAD(ready_queue_head, process_s) ready_vh_queue; // Very High Priority Queue
    TAILQ_HEAD(ready_high_queue, process_s) ready_h_queue; // High Priority Queue
    TAILQ_HEAD(ready_medium_queue, process_s) ready_m_and_l_queue; // Medium and Low Priority Queue

    TAILQ_HEAD(waiting_queue_head, process_s) waiting_queue; // Waiting Queue
} Scheduler;

int scheduler_init(Scheduler* scheduler, size_t pid);
void scheduler_destroy(Scheduler* scheduler);
void scheduler_loop(Scheduler* scheduler);
