#pragma once

#include <process.h>
#include <pthread.h>
#include <utils/run_queue.h>

#define NB_RQ 4 // Number of run queues

typedef struct scheduler_s {
    pthread_t system_thread_id;
    Pid pid;

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

    aco_t* scheduler_acoro;
} Scheduler;

// Time a scheduler sleeps when none of its processes is ready
#define SCHEDULER_IDLE_SLEEP_US 1000

int scheduler_init(Scheduler* scheduler, Pid pid);
void scheduler_destroy(Scheduler* scheduler);
void scheduler_loop(Scheduler* scheduler);

// Thread-safe push to the scheduler
void scheduler_push_shared(Scheduler* scheduler, Process* process);

// Non-thread-safe push to the scheduler (only in the scheduler thread)
void scheduler_push(Scheduler* scheduler, Process* process);

Process* pick_process(Scheduler* scheduler);
