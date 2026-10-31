#pragma once

#include <process.h>
#include <pthread.h>
#include <utils/run_queue.h>
#include <utils/idle.h>
#include <timer.h>

// Times an idle scheduler polls for work before going to sleep
#define SCHEDULER_IDLE_SPINS 2000
// While another scheduler has processes waiting to run, an idle one asks again this often
#define SCHEDULER_STEAL_RETRY_MS 5

#define NB_RQ 3 // Number of run queues

// Free process table slots a scheduler keeps, to spawn and end processes
// without the table lock; it trades them with the table by batches
#define SLOT_CACHE_SIZE  64
#define SLOT_CACHE_BATCH 32

typedef struct {
    int slots[SLOT_CACHE_SIZE];
    int count;
} SlotCache;

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
    // Idle but awake, looking for work: an overloaded scheduler need not wake another
    atomic_bool searching;

    aco_t *scheduler_acoro;

    SlotCache slot_cache; // Its own thread only
    TimerQueue timers;    // Armed by the processes it runs, fired in its loop
    // When it sleeps: the deadline it sleeps until (0: none), so that an earlier
    // timer armed by another thread wakes it (scheduler thread only)
    uint64_t sleep_deadline;

    // For the watchdog: when the current process was resumed (0: none runs), and which
    _Atomic uint64_t running_since;
    _Atomic Pid running_pid;
} Scheduler;

int scheduler_init(Scheduler *scheduler, Pid pid);
void scheduler_destroy(Scheduler *scheduler);
void scheduler_loop(Scheduler *scheduler);

// The scheduler running on the calling thread, or NULL outside the schedulers
Scheduler *scheduler_current(void);
// Scheduler thread, when its loop starts
void scheduler_set_current(Scheduler *scheduler);

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
