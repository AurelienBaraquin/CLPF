#include <clpf_internal.h>
#include <api/base.h>
#include <stdio.h>
#include <string.h>
#include <pthread.h>
#include <utils/cpu.h>
#include <flags_internal.h>
#include <utils/time.h>
#include <watchdog.h>
#include <memory/reaper.h>
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

static bool zeroed_mutex_is_valid(void) {
    const pthread_mutex_t initializer = PTHREAD_MUTEX_INITIALIZER;
    unsigned char bytes[sizeof initializer];
    memcpy(bytes, &initializer, sizeof bytes);
    for (size_t i = 0; i < sizeof bytes; ++i) {
        if (bytes[i] != 0)
            return false;
    }
    return true;
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

    // The table stays untouched zeroed memory until slots are used: a zeroed
    // slot is free (pid PID_FREE, generation 0), and so is its lock where a
    // zeroed mutex is valid, as with glibc. Elsewhere, initialize them all.
    if (!zeroed_mutex_is_valid()) {
        for (int i = 0; i < MAX_PROCESSES; i++)
            pthread_mutex_init(&clpf.process_table[i].lock, NULL);
    }

    // Slots are handed out in order, then reused from free_slots once released
    clpf.free_slot_count = 0;
    atomic_store(&clpf.slot_end, 1);

    clpf.process_count = 0;
    clpf.scheduler_count = 0;
    pthread_mutex_init(&clpf.table_lock, NULL);

    idle_init(&clpf.main_idle);
    clpf_log_init();
    watchdog_init();
    // The main thread unmaps the memory of ended processes, off the schedulers
    reaper_set_wake(clpf_wake_main);
    return 0;
}

int clpf_slot_alloc(void) {
    int index = -1;

    pthread_mutex_lock(&clpf.table_lock);
    int end = atomic_load(&clpf.slot_end);
    if (clpf.free_slot_count > 0) {
        index = clpf.free_slots[--clpf.free_slot_count];
    } else if (end < MAX_PROCESSES) {
        index = end;
        atomic_store(&clpf.slot_end, end + 1);
    }
    pthread_mutex_unlock(&clpf.table_lock);
    return index;
}

int clpf_slot_end(void) {
    return atomic_load(&clpf.slot_end);
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

    int end = clpf_slot_end();
    for (int i = 1; i < end; i++)
        process_destroy(&clpf.process_table[i]);
    for (int i = 1; i < end; i++)
        pthread_mutex_destroy(&clpf.process_table[i].lock);
    free(clpf.process_table);
    free(clpf.free_slots);
    pthread_mutex_destroy(&clpf.table_lock);

    name_table_free(clpf.name_table);

    reaper_run();
    stack_pool_drain();
    safe_heap_drain_pools();
    idle_destroy(&clpf.main_idle);
}

void clpf_wake_all(void) {
    clpf_wake_main();
    for (int i = 0; i < clpf.scheduler_count; i++)
        scheduler_wake(&clpf.scheduler_table[i]);
}

// Main thread, during shutdown: ends every process that is not a system one
static void kill_user_processes(void) {
    int end = clpf_slot_end();
    for (int i = 1; i < end; i++) {
        Process *process = &clpf.process_table[i];
        pthread_mutex_lock(&process->lock);
        if (!process_slot_free(process) && !process->system)
            process_exit_signal_locked(process, INVALID_PID, EXIT_REASON_KILLED, true);
        pthread_mutex_unlock(&process->lock);
    }
}

void clpf_wake_main(void) {
    atomic_fetch_add(&clpf.main_events, 1);
    idle_wake(&clpf.main_idle);
}

// arg: the event count when the main thread last looked at everything
static bool main_has_work(void *arg) {
    return exit_in_progress || atomic_load(&clpf.main_events) != *(unsigned int *)arg;
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
        atomic_store(&clpf.entry_exit_reason, EXIT_REASON_ERROR);

    // Once the entry process ended: kill the user processes, then ask the
    // system servers (I/O last) to stop, then wait until every process is gone
    bool users_killed = false;
    bool system_stopped = false;

    while (1) {
        // Changes made from now on bump the count: the wait below sees them
        unsigned int events = atomic_load(&clpf.main_events);
        size_t total_process_count = 0;
        for (int i = 0; i < clpf.scheduler_count; ++i)
            total_process_count += atomic_load(&clpf.scheduler_table[i].process_count);

        reaper_run();

        // Schedulers count a process out once they destroyed it
        if (total_process_count == 0)
            break;

        if (atomic_load(&clpf.shutdown_requested)) {
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
        idle_wait_until(&clpf.main_idle, main_has_work, &events, next_check);
    }

    exit_in_progress = 1;
    clpf_wake_all();

    for (int i = 0; i < clpf.scheduler_count; i++) {
        Scheduler *scheduler = &clpf.scheduler_table[i];
        pthread_join(scheduler->system_thread_id, NULL);
    }

    clpf_dump_signal_stop();
    clpf_destroy();
    return atomic_load(&clpf.entry_exit_reason) == EXIT_REASON_NORMAL ? 0 : 1;
}
