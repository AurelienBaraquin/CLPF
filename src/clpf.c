#include <clpf_internal.h>
#include <stdio.h>
#include <pthread.h>
#include <utils/cpu.h>
#include <flags_internal.h>

void init_task(Task entry, int argc, char *argv[]);

#define RESERVED_CPUS 2

CLPF clpf = {0};

static void *scheduler_thread(void *arg)
{
    scheduler_loop((Scheduler *)arg);
    return NULL;
}

static int clpf_init(void)
{
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
    return 0;
}

int clpf_slot_alloc(void)
{
    int index = -1;

    pthread_mutex_lock(&clpf.table_lock);
    if (clpf.free_slot_count > 0)
        index = clpf.free_slots[--clpf.free_slot_count];
    pthread_mutex_unlock(&clpf.table_lock);
    return index;
}

void clpf_slot_release(int index)
{
    pthread_mutex_lock(&clpf.table_lock);
    clpf.free_slots[clpf.free_slot_count++] = index;
    pthread_mutex_unlock(&clpf.table_lock);
}

static void clpf_destroy(void)
{
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

    for (MPSCQNode *n = mpscq_drain(&clpf.message_queue); n != NULL; ) {
        MPSCQNode *next = n->next;
        __clpf_message_destroy(container_of(n, CoreMessage, node));
        n = next;
    }

    idle_destroy(&clpf.main_idle);
}

void clpf_post(CoreMessage *msg)
{
    mpscq_push(&clpf.message_queue, &msg->node);
    idle_wake(&clpf.main_idle);
}

void clpf_wake_all(void)
{
    idle_wake(&clpf.main_idle);
    for (int i = 0; i < clpf.scheduler_count; i++)
        scheduler_wake(&clpf.scheduler_table[i]);
}

static bool main_has_work(void *arg)
{
    (void)arg;
    return exit_in_progress || !mpscq_empty(&clpf.message_queue);
}

void __clpf_main(Task entry_point, int argc, char *argv[])
{
    if (!entry_point) {
        fprintf(stderr, "Entry point is NULL\n");
        return;
    }

    if (clpf_init() != 0)
        return;

    int cpus = nb_cpu();
    if (cpus <= 0) {
        perror("Failed to get number of CPUs");
        return;
    }

    // Two cores are left to the main thread and the system, but there is always one scheduler
    int schedulers = cpus > RESERVED_CPUS ? cpus - RESERVED_CPUS : 1;

    for (int i = 0; i < MAX_SCHEDULERS && i < schedulers; i++) {
        if (scheduler_init(&clpf.scheduler_table[i], i) != 0) {
            fprintf(stderr, "Failed to initialize scheduler %d\n", i);
            return;
        }
        ++clpf.scheduler_count;
    }

    for (int i = 0; i < clpf.scheduler_count; i++) {
        Scheduler* scheduler = &clpf.scheduler_table[i];
        int r = pthread_create(&scheduler->system_thread_id, NULL, scheduler_thread, scheduler);
        if (r != 0) {
            fprintf(stderr, "Failed to create scheduler thread %d\n", i);
            return;
        }
    }

    init_task(entry_point, argc, argv);

    while (exit_in_progress == 0) {
        size_t total_process_count = 0;

        for (int i = 0; i < clpf.scheduler_count; ++i) {
            total_process_count += atomic_load(&clpf.scheduler_table[i].process_count);
        }

        int updated = __clpf_main_update();

        // Check if all processes are finished
        if (total_process_count <= 0 && mpscq_empty(&clpf.message_queue) && updated == 0) {
            break;
        }

        if (updated == 0)
            idle_wait(&clpf.main_idle, main_has_work, NULL);
    }

    exit_in_progress = 1;
    clpf_wake_all();

    for (int i = 0; i < MAX_PROCESSES; ++i) {
        Process* process = &clpf.process_table[i];
        if (process->pid != INVALID_PID) {
            send(NULL, process->pid, NULL, 0, ((Tag){ .kind = TAG_KIND_STRING, .str_value = ("core::event::stop") }));
        }
    }

    for (int i = 0; i < clpf.scheduler_count; i++) {
        Scheduler* scheduler = &clpf.scheduler_table[i];
        pthread_join(scheduler->system_thread_id, NULL);
    }

    clpf_destroy();
}
