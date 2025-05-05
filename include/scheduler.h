#pragma once

#include <process.h>
#include <pthread.h>
#include <utils/run_queue.h>

typedef enum scheduler_migration_state_e {
    SCHEDULER_IMMIGRATING,
    SCHEDULER_EMIGRATING,
    SCHEDULER_BALANCED,
} scheduler_migration_state;

#define NB_RQ 4 // Number of run queues

typedef struct scheduler_s {
    pthread_t system_thread_id;
    Pid pid;

    struct scheduler_s* immigrate_from;
    struct scheduler_s* emigrate_to;

    // 0 -> VERY_HIGH
    // 1 -> HIGH
    // 2 -> MEDIUM & LOW
    // 3 -> WAITING
    MPSCQ shared_rq[NB_RQ];

    // 0 -> VERY_HIGH
    // 1 -> HIGH
    // 2 -> MEDIUM & LOW
    // 3 -> WAITING
    RunQueue rq[NB_RQ];

    atomic_int process_count;

    pthread_mutex_t mutex;
    pthread_cond_t cond;

    cothread_t* scheduler_coro;
} Scheduler;

#define LOCK_UNTIL(predicate, cond, mutex) \
    do { \
        pthread_mutex_lock(mutex); \
        while (!(predicate)) { \
            pthread_cond_wait(cond, mutex); \
        } \
        pthread_mutex_unlock(mutex); \
    } while (0)

int scheduler_init(Scheduler* scheduler, Pid pid);
void scheduler_destroy(Scheduler* scheduler);
void scheduler_loop(Scheduler* scheduler);

// Thread-safe push to the scheduler
void scheduler_push_shared(Scheduler* scheduler, Process* process);

// Non-thread-safe push to the scheduler (only in the scheduler thread)
void scheduler_push(Scheduler* scheduler, Process* process);

Process* pick_process(Scheduler* scheduler);
