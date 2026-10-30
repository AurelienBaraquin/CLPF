#include <clpf_internal.h>
#include <api/base.h>
#include <stdio.h>
#include <pthread.h>
#include <utils/cpu.h>
#include <flags_internal.h>
#include <utils/time.h>
#include <watchdog.h>
#include <utils/stack_guard.h>
#include <timer.h>

// Starts the launcher, which starts the system servers and then the entry task
int init_task(Task entry, int argc, char *argv[]);

#define RESERVED_CPUS 2

CLPF clpf = {0};

static void *scheduler_thread(void *arg) {
    scheduler_loop((Scheduler *)arg);
    return NULL;
}

static int clpf_init(void) {
    clpf.process_table = calloc(MAX_PROCESSES, sizeof(Process));
    clpf.free_slots = malloc(MAX_PROCESSES * sizeof(int));
    clpf.name_table = name_table_init();
    if (!clpf.process_table || !clpf.free_slots || !clpf.name_table) {
        fprintf(stderr, "Failed to allocate the runtime tables\n");
        free(clpf.process_table);
        free(clpf.free_slots);
        name_table_free(clpf.name_table);
        return -1;
    }

    for (int i = 0; i < MAX_PROCESSES; i++) {
        pthread_mutex_init(&clpf.process_table[i].lock, NULL);
        clpf.process_table[i].pid = INVALID_PID;
        clpf.process_table[i].generation = 0;
    }

    // Lowest indices on top, so the first processes get the smallest pids
    clpf.free_slot_count = MAX_PROCESSES;
    for (int i = 0; i < MAX_PROCESSES; i++)
        clpf.free_slots[i] = MAX_PROCESSES - 1 - i;

    clpf.process_count = 0;
    clpf.scheduler_count = 0;
    pthread_mutex_init(&clpf.table_lock, NULL);

    mpscq_init(&clpf.message_queue);
    idle_init(&clpf.main_idle);
    timers_init();
    clpf_log_init();
    watchdog_init();
    return 0;
}

int clpf_slot_alloc(void) {
    int index = -1;

    pthread_mutex_lock(&clpf.table_lock);
    if (clpf.free_slot_count > 0)
        index = clpf.free_slots[--clpf.free_slot_count];
    pthread_mutex_unlock(&clpf.table_lock);
    return index;
}

void clpf_slot_release(int index) {
    pthread_mutex_lock(&clpf.table_lock);
    clpf.free_slots[clpf.free_slot_count++] = index;
    pthread_mutex_unlock(&clpf.table_lock);
}

static void clpf_destroy(void) {
    for (int i = 0; i < clpf.scheduler_count; i++) {
        scheduler_destroy(&clpf.scheduler_table[i]);
    }

    for (int i = 0; i < MAX_PROCESSES; i++) {
        process_destroy(&clpf.process_table[i]);
    }

    for (int i = 0; i < MAX_PROCESSES; i++) {
        pthread_mutex_destroy(&clpf.process_table[i].lock);
    }
    free(clpf.process_table);
    free(clpf.free_slots);
    pthread_mutex_destroy(&clpf.table_lock);

    name_table_free(clpf.name_table);

    for (MPSCQNode *n = mpscq_drain(&clpf.message_queue); n != NULL;) {
        MPSCQNode *next = n->next;
        clpf_message_destroy(container_of(n, CoreMessage, node));
        n = next;
    }

    stack_pool_drain();
    safe_heap_drain_pools();
    timers_destroy();
    idle_destroy(&clpf.main_idle);
}

void clpf_post(CoreMessage *msg) {
    mpscq_push(&clpf.message_queue, &msg->node);
    idle_wake(&clpf.main_idle);
}

void clpf_wake_all(void) {
    idle_wake(&clpf.main_idle);
    for (int i = 0; i < clpf.scheduler_count; i++)
        scheduler_wake(&clpf.scheduler_table[i]);
}

// Main thread, during shutdown: ends every process that is not a system one
static void kill_user_processes(void) {
    for (int i = 0; i < MAX_PROCESSES; i++) {
        Process *process = &clpf.process_table[i];
        pthread_mutex_lock(&process->lock);
        if (process->pid != INVALID_PID && !process->system)
            process_exit_signal_locked(process, INVALID_PID, EXIT_REASON_KILLED, true);
        pthread_mutex_unlock(&process->lock);
    }
}

static bool main_has_work(void *arg) {
    (void)arg;
    return exit_in_progress || !mpscq_empty(&clpf.message_queue);
}

int clpf_main(Task entry_point, int argc, char *argv[]) {
    if (!entry_point) {
        fprintf(stderr, "Entry point is NULL\n");
        return 1;
    }

    if (clpf_init() != 0)
        return 1;
    clpf_dump_signal_init();
    stack_guard_install();

    int cpus = nb_cpu();
    if (cpus <= 0) {
        perror("Failed to get number of CPUs");
        return 1;
    }

    // Two cores are left to the main thread and the system, but there is always one scheduler
    int schedulers = cpus > RESERVED_CPUS ? cpus - RESERVED_CPUS : 1;

    for (int i = 0; i < MAX_SCHEDULERS && i < schedulers; i++) {
        if (scheduler_init(&clpf.scheduler_table[i], i) != 0) {
            fprintf(stderr, "Failed to initialize scheduler %d\n", i);
            return 1;
        }
        ++clpf.scheduler_count;
    }

    for (int i = 0; i < clpf.scheduler_count; i++) {
        Scheduler *scheduler = &clpf.scheduler_table[i];
        int r = pthread_create(&scheduler->system_thread_id, NULL, scheduler_thread, scheduler);
        if (r != 0) {
            fprintf(stderr, "Failed to create scheduler thread %d\n", i);
            return 1;
        }
    }

    // Without a launcher no process exists: the loop below ends at once
    if (init_task(entry_point, argc, argv) != 0)
        clpf.entry_exit_reason = EXIT_REASON_ERROR;

    // Once the entry process ended: kill the user processes, then ask the
    // system servers (I/O last) to stop, then wait until every process is gone
    bool users_killed = false;
    bool system_stopped = false;

    while (1) {
        size_t total_process_count = 0;
        for (int i = 0; i < clpf.scheduler_count; ++i)
            total_process_count += atomic_load(&clpf.scheduler_table[i].process_count);

        int updated = clpf_main_update();
        uint64_t next_timer = timers_fire_due();

        if (total_process_count == 0 && mpscq_empty(&clpf.message_queue) && updated == 0)
            break;

        if (clpf.shutdown_requested && updated == 0) {
            pthread_mutex_lock(&clpf.table_lock);
            int users = clpf.user_process_count;
            pthread_mutex_unlock(&clpf.table_lock);

            // Killed processes may spawn before their scheduler ends them: sweep until none is left
            if (users > 0 && !users_killed) {
                kill_user_processes();
                users_killed = true;
                continue;
            }
            if (users > 0) {
                users_killed = false;
            } else if (!system_stopped) {
                clpf_signal_exit(atomic_load(&clpf.system_supervisor), EXIT_REASON_SHUTDOWN, false);
                system_stopped = true;
            }
        }

        uint64_t next_check = watchdog_check(now_ms());
        if (!next_timer || (next_check && next_check < next_timer))
            next_timer = next_check;
        if (updated == 0)
            idle_wait_until(&clpf.main_idle, main_has_work, NULL, next_timer);
    }

    exit_in_progress = 1;
    clpf_wake_all();

    for (int i = 0; i < clpf.scheduler_count; i++) {
        Scheduler *scheduler = &clpf.scheduler_table[i];
        pthread_join(scheduler->system_thread_id, NULL);
    }

    clpf_dump_signal_stop();
    clpf_destroy();
    return clpf.entry_exit_reason == EXIT_REASON_NORMAL ? 0 : 1;
}
