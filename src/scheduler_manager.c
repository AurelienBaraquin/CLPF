#include <scheduler_manager.h>
#include <stdlib.h>

int scheduler_compare(const void* a, const void* b)
{
    Scheduler* scheduler_a = (Scheduler*)a;
    Scheduler* scheduler_b = (Scheduler*)b;

    if (scheduler_a->process_count > scheduler_b->process_count) {
        return -1;
    } else if (scheduler_a->process_count < scheduler_b->process_count) {
        return 1;
    } else {
        return 0;
    }
}

void sch_mg_sort(void)
{
    qsort(clpf.scheduler_table, clpf.scheduler_count, sizeof(Scheduler), scheduler_compare);
}

Pid sch_mg_get_less_process_scheduler(void)
{
    if (clpf.scheduler_count == 0)
        return -1;

    Pid min_pid = clpf.scheduler_table[0].pid;
    for (int i = 0; i < clpf.scheduler_count; i++) {
        if (clpf.scheduler_table[i].process_count < clpf.scheduler_table[min_pid].process_count) {
            min_pid = clpf.scheduler_table[i].pid;
        }
    }

    return min_pid;
}
