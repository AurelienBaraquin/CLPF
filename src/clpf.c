#include <clpf.h>
#include <stdio.h>
#include <pthread.h>
#include <utils/cpu.h>

void clpf(Task entry_point)
{
    CLPF clpf = {0};

    int cpus = nb_cpu();
    if (cpus < 0) {
        perror("Failed to get number of CPUs");
        return;
    }

    for (int i = 0; i < MAX_SCHEDULERS && i < cpus; i++) {
        if (scheduler_init(&clpf.scheduler_table[i], i) != 0) {
            fprintf(stderr, "Failed to initialize scheduler %d\n", i);
            return;
        }
        ++clpf.scheduler_count;
    }

    for (int i = 0; i < clpf.scheduler_count; i++) {
        Scheduler* scheduler = &clpf.scheduler_table[i];
        int r = pthread_create(&scheduler->system_thread_id, NULL, (void *(*)(void *))scheduler_loop, scheduler);
        if (r != 0) {
            fprintf(stderr, "Failed to create scheduler thread %d\n", i);
            return;
        }
    }

    for (int i = 0; i < clpf.scheduler_count; i++) {
        Scheduler* scheduler = &clpf.scheduler_table[i];
        pthread_join(scheduler->system_thread_id, NULL);
    }
}
