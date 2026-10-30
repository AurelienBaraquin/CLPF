#pragma once

#include <process.h>
#include <pthread.h>
#include <utils/run_queue.h>
#include <utils/idle.h>

// Times an idle scheduler polls for work before going to sleep
#define SCHEDULER_IDLE_SPINS 2000
// While another scheduler has processes waiting to run, an idle one asks again this often
#define SCHEDULER_STEAL_RETRY_MS 5

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

    // Bit i set: idle scheduler i asks for work (see scheduler_request_work)
    _Atomic uint64_t steal_requests;
    // Processes in the private run queues, published after each pick
    atomic_int ready_count;

    aco_t *scheduler_acoro;

    // For the watchdog: when the current process was resumed (0: none runs), and which
    _Atomic uint64_t running_since;
    _Atomic Pid running_pid;
} Scheduler;

int scheduler_init(Scheduler *scheduler, Pid pid);
void scheduler_destroy(Scheduler *scheduler);
void scheduler_loop(Scheduler *scheduler);

// Thread-safe push to the scheduler
void scheduler_push_shared(Scheduler *scheduler, Process *process);

// Thread-safe: wakes the scheduler if it sleeps, after work was published for it
void scheduler_wake(Scheduler *scheduler);

// Non-thread-safe push to the scheduler (only in the scheduler thread)
void scheduler_push(Scheduler *scheduler, Process *process);

// Moves the waiting processes that got a message or a signal back to a run queue (scheduler thread only)
void scheduler_drain_wakeups(Scheduler *scheduler);

Process *pick_process(Scheduler *scheduler);

// Called by an idle scheduler: asks the one with the most ready processes to hand
// over some. Returns false if no scheduler has work to spare.
bool scheduler_request_work(Scheduler *thief);
