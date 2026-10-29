#include <clpf_internal.h>
#include <stdio.h>
#include <pthread.h>
#include <utils/cpu.h>
#include <flags_internal.h>

void init_task(Task entry, int argc, char *argv[]);

CLPF clpf = {0};

static void *scheduler_thread(void *arg)
{
    scheduler_loop((Scheduler *)arg);
    return NULL;
}

void clpf_init()
{
    for (int i = 0; i < MAX_PROCESSES; i++) {
        pthread_mutex_init(&clpf.process_table[i].lock, NULL);
        clpf.process_table[i].pid = INVALID_PID;
        clpf.process_table[i].generation = 0;
    }

    clpf.process_count = 0;
    clpf.scheduler_count = 0;

    clpf.name_table = name_table_init();
    if (!clpf.name_table) {
        fprintf(stderr, "Failed to initialize name table\n");
        return;
    }

    clpf.fix = 0;
    clpf.tix = 0;

    global_supervisor_init(&clpf.global_supervisor);

    mpscq_init(&clpf.message_queue);
}

void clpf_destroy()
{
    for (int i = 0; i < clpf.scheduler_count; i++) {
        scheduler_destroy(&clpf.scheduler_table[i]);
    }

    for (int i = 0; i < clpf.process_count; i++) {
        process_destroy(&clpf.process_table[i]);
    }

    for (int i = 0; i < MAX_PROCESSES; i++) {
        pthread_mutex_destroy(&clpf.process_table[i].lock);
    }

    name_table_free(clpf.name_table);

    global_supervisor_destroy(&clpf.global_supervisor);

    mpscq_clear(&clpf.message_queue, free);
}

void __clpf_main(Task entry_point, int argc, char *argv[])
{
    if (!entry_point) {
        fprintf(stderr, "Entry point is NULL\n");
        return;
    }

    clpf_init();

    int cpus = nb_cpu() - 1;
    if (cpus <= 0) {
        perror("Failed to get number of CPUs");
        return;
    }

    for (int i = 0; i < MAX_SCHEDULERS && i < cpus - 1; i++) {
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
        usleep(1000); // Sleep for 1ms to avoid busy waiting

        size_t total_process_count = 0;

        for (int i = 0; i < clpf.scheduler_count; ++i) {
            total_process_count += atomic_load(&clpf.scheduler_table[i].process_count);
        }

        int updated = __clpf_main_update();

        // Check if all processes are finished
        if (total_process_count <= 0 && mpscq_empty(&clpf.message_queue) && updated == 0) {
            break;
        }
    }

    exit_in_progress = 1;

    for (int i = 0; i < clpf.process_count; ++i) {
        Process* process = &clpf.process_table[i];
        if (process->pid != -1) {
            send(NULL, process->pid, NULL, 0, ((Tag){ .kind = TAG_KIND_STRING, .str_value = ("core::event::stop") }));
        }
    }

    for (int i = 0; i < clpf.scheduler_count; i++) {
        Scheduler* scheduler = &clpf.scheduler_table[i];
        pthread_join(scheduler->system_thread_id, NULL);
    }

    clpf_destroy();
}
