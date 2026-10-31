#include <scheduler.h>
#include <clpf_internal.h>

static _Thread_local Scheduler *current_scheduler;

Scheduler *scheduler_current(void) {
    return current_scheduler;
}

void scheduler_set_current(Scheduler *scheduler) {
    current_scheduler = scheduler;
}

int scheduler_init(Scheduler *scheduler, Pid pid) {
    if (!scheduler)
        return -1;

    scheduler->pid = pid;
    atomic_init(&scheduler->process_count, 0);

    for (int i = 0; i < NB_RQ; i++) {
        mpscq_init(&scheduler->shared_rq[i]);
    }
    mpscq_init(&scheduler->wakeups);
    atomic_init(&scheduler->steal_requests, 0);
    atomic_init(&scheduler->ready_count, 0);
    atomic_init(&scheduler->searching, false);
    atomic_init(&scheduler->running_since, 0);
    atomic_init(&scheduler->running_pid, INVALID_PID);
    idle_init(&scheduler->idle);
    timer_queue_init(&scheduler->timers, (int)pid);
    scheduler->sleep_deadline = 0;
    for (int i = 0; i < NB_RQ; i++) {
        runq_init(&scheduler->rq[i]);
    }

    scheduler->system_thread_id = -1;

    scheduler->scheduler_acoro = NULL;

    return 0;
}

void scheduler_destroy(Scheduler *scheduler) {
    if (!scheduler)
        return;

    for (int i = 0; i < NB_RQ; i++) {
        runq_clear(&scheduler->rq[i], NULL);
    }

    if (scheduler->scheduler_acoro)
        aco_destroy(scheduler->scheduler_acoro);

    timer_queue_destroy(&scheduler->timers);
    idle_destroy(&scheduler->idle);
}

void scheduler_push_shared(Scheduler *scheduler, Process *process) {
    if (!scheduler || !process)
        return;

    switch (process->priority) {
        case PROCESS_PRIORITY_LOW:
        case PROCESS_PRIORITY_MEDIUM: mpscq_push(&scheduler->shared_rq[2], &process->shared_node); break;
        case PROCESS_PRIORITY_HIGH: mpscq_push(&scheduler->shared_rq[1], &process->shared_node); break;
        case PROCESS_PRIORITY_VERY_HIGH: mpscq_push(&scheduler->shared_rq[0], &process->shared_node); break;
        default: break;
    }

    scheduler_wake(scheduler);
}

void scheduler_wake(Scheduler *scheduler) {
    idle_wake(&scheduler->idle);
}

void scheduler_push(Scheduler *scheduler, Process *process) {
    if (!scheduler || !process)
        return;

    switch (process->priority) {
        case PROCESS_PRIORITY_LOW:
        case PROCESS_PRIORITY_MEDIUM: runq_push(&scheduler->rq[2], process); break;
        case PROCESS_PRIORITY_HIGH: runq_push(&scheduler->rq[1], process); break;
        case PROCESS_PRIORITY_VERY_HIGH: runq_push(&scheduler->rq[0], process); break;
        default: break;
    }
}

void scheduler_drain(Scheduler *scheduler) {
    if (!scheduler)
        return;

    for (int i = 0; i < NB_RQ; i++) {
        MPSCQNode *node = mpscq_drain(&scheduler->shared_rq[i]);
        while (node) {
            Process *process = container_of(node, Process, shared_node);
            node = node->next;
            runq_push(&scheduler->rq[i], process);
        }
    }
}

void scheduler_drain_wakeups(Scheduler *scheduler) {
    MPSCQNode *node = mpscq_drain(&scheduler->wakeups);
    while (node) {
        Process *process = container_of(node, Process, wake_node);
        node = node->next;

        // Cleared before looking at the state: a later delivery queues the process again
        atomic_store(&process->wake_pending, false);
        if (process->state == PROCESS_WAITING) {
            process->state = PROCESS_READY;
            scheduler_push(scheduler, process);
        }
    }
}

bool scheduler_request_work(Scheduler *thief) {
    Scheduler *victim = NULL;
    int most = 1; // with a single ready process, its scheduler is not overloaded
    for (int i = 0; i < clpf.scheduler_count; i++) {
        Scheduler *candidate = &clpf.scheduler_table[i];
        int ready = atomic_load(&candidate->ready_count);
        if (candidate != thief && ready > most) {
            victim = candidate;
            most = ready;
        }
    }

    if (victim)
        atomic_fetch_or(&victim->steal_requests, (uint64_t)1 << thief->pid);
    return victim != NULL;
}

// Moves a ready process to another scheduler; it must be in no queue of this one.
// A process with a wakeup queued here stays: this scheduler will still read its state.
static bool migrate(Scheduler *from, Scheduler *to, Process *process) {
    pthread_mutex_lock(&process->lock);
    bool movable = !atomic_load(&process->wake_pending);
    if (movable) {
        process->scheduler_pid = to->pid;
        // A started coroutine returns to the thread that resumes it
        if (process->acoro)
            process->acoro->main_co = to->scheduler_acoro;
    }
    pthread_mutex_unlock(&process->lock);

    if (movable) {
        atomic_fetch_sub(&from->process_count, 1);
        atomic_fetch_add(&to->process_count, 1);
        scheduler_push_shared(to, process);
    }
    return movable;
}

static size_t ready_processes(Scheduler *scheduler) {
    size_t ready = 0;
    for (int i = 0; i < NB_RQ; i++)
        ready += runq_size(&scheduler->rq[i]);
    return ready;
}

// Shares the ready processes evenly with the thieves, least urgent first
static void give_work(Scheduler *scheduler, uint64_t thieves) {
    size_t share = ready_processes(scheduler) / ((size_t)__builtin_popcountll(thieves) + 1);

    for (int t = 0; t < clpf.scheduler_count && share > 0; t++) {
        if (!(thieves & ((uint64_t)1 << t)))
            continue;

        Scheduler *thief = &clpf.scheduler_table[t];
        size_t to_give = share;
        for (int i = NB_RQ - 1; i >= 0 && to_give > 0; i--) {
            RunQueue *rq = &scheduler->rq[i];
            size_t n = runq_size(rq);
            for (size_t j = 0; j < n && to_give > 0; j++) {
                Process *process = runq_pop(rq);
                if (migrate(scheduler, thief, process))
                    to_give--;
                else
                    runq_push(rq, process);
            }
        }
    }
}

static Process *pick_ready(Scheduler *scheduler);

// Overloaded: wakes one sleeping scheduler to come and take some work, unless
// an idle one is already awake and looking for it. Once that one is busy, the
// next pick wakes another if the work is still there.
static void wake_helper(Scheduler *busy) {
    Scheduler *sleeper = NULL;
    for (int i = 0; i < clpf.scheduler_count; i++) {
        Scheduler *other = &clpf.scheduler_table[i];
        if (other == busy)
            continue;
        if (atomic_load(&other->searching))
            return;
        if (!sleeper && atomic_load(&other->idle.sleeping))
            sleeper = other;
    }
    if (sleeper)
        scheduler_wake(sleeper);
}

Process *pick_process(Scheduler *scheduler) {
    if (!scheduler)
        return NULL;

    scheduler_drain_wakeups(scheduler);
    scheduler_drain(scheduler);

    uint64_t thieves = atomic_exchange(&scheduler->steal_requests, 0);
    if (thieves)
        give_work(scheduler, thieves);

    Process *picked = pick_ready(scheduler);
    int ready = (int)ready_processes(scheduler);
    atomic_store(&scheduler->ready_count, ready);

    if (ready >= 2)
        wake_helper(scheduler);
    return picked;
}

static Process *pick_ready(Scheduler *scheduler) {
    for (int i = 0; i < NB_RQ; ++i) {
        RunQueue *rq = &scheduler->rq[i];

        size_t n = runq_size(rq);
        for (size_t j = 0; j < n; ++j) {
            Process *process = runq_pop(rq);

            // Only low priority processes are ever delayed
            if (process->schedule_count > 0) {
                process->schedule_count--;
                runq_push(rq, process);
                continue;
            }

            if (process->priority == PROCESS_PRIORITY_LOW)
                process->schedule_count = LOW_PRIORITY_SKIPS;

            return process;
        }
    }

    // Only delayed low priority processes are ready: run one rather than sleep
    Process *process = runq_pop(&scheduler->rq[NB_RQ - 1]);
    if (process)
        process->schedule_count = LOW_PRIORITY_SKIPS;
    return process;
}
