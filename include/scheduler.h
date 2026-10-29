#pragma once

#include <process.h>
#include <pthread.h>
#include <utils/run_queue.h>
#include <utils/idle.h>

// Times an idle scheduler polls for work before going to sleep
#define SCHEDULER_IDLE_SPINS 2000

#define NB_RQ 3 // Number of run queues

typedef struct scheduler_s {
    pthread_t system_thread_id;
    Pid pid;

    // 0 -> VERY_HIGH
    // 1 -> HIGH
    // 2 -> MEDIUM & LOW
    MPSCQ shared_rq[NB_RQ];
    RunQueue rq[NB_RQ];

    // Processes that received a message or a signal. Waiting processes are in
    // no run queue: they come back through here, so a pick never scans them.
    MPSCQ wakeups;

    atomic_int process_count;

    IdleWaiter idle; // The scheduler sleeps here when none of its processes is ready

    aco_t* scheduler_acoro;
} Scheduler;


int scheduler_init(Scheduler* scheduler, Pid pid);
void scheduler_destroy(Scheduler* scheduler);
void scheduler_loop(Scheduler* scheduler);

// Thread-safe push to the scheduler
void scheduler_push_shared(Scheduler* scheduler, Process* process);

// Thread-safe: wakes the scheduler if it sleeps, after work was published for it
void scheduler_wake(Scheduler* scheduler);

// Non-thread-safe push to the scheduler (only in the scheduler thread)
void scheduler_push(Scheduler* scheduler, Process* process);

// Moves the waiting processes that got a message or a signal back to a run queue (scheduler thread only)
void scheduler_drain_wakeups(Scheduler* scheduler);

Process* pick_process(Scheduler* scheduler);
