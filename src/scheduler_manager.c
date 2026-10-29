#include <scheduler_manager.h>

Pid sch_mg_get_less_process_scheduler(void) {
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
