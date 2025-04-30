#include <clpf_internal.h>
#include <scheduler_manager.h>
#include <stdio.h>

void clpf_task_add(Task task, process_priority priority, Pid parent_pid)
{
    // find an empty process (when id is 0)
    int empty_process = -1;
    for (int i = 0; i < MAX_PROCESSES; i++) {
        if (clpf.process_table[i].pid == 0) {
            empty_process = i;
            break;
        }
    }

    if (empty_process == -1) {
        fprintf(stderr, "No empty process available\n");
        return;
    }

    // create the process
    Process *process = &clpf.process_table[empty_process];
    if (process_init(process, task, priority, empty_process, parent_pid) != 0) {
        fprintf(stderr, "Failed to initialize process %d\n", empty_process);
        return;
    }

    Scheduler *scheduler = NULL;

    if (parent_pid == -1) {
        Pid less_process_scheduler = sch_mg_get_less_process_scheduler();
        if (less_process_scheduler == -1) {
            fprintf(stderr, "No scheduler available\n");
            process_destroy(process);
            return;
        }
        scheduler = &clpf.scheduler_table[less_process_scheduler];
    } else {
        // take the scheduler of the parent process
        scheduler = &clpf.scheduler_table[clpf.process_table[parent_pid].scheduler_pid];
    }

    if (!scheduler) {
        // take the first scheduler
        scheduler = &clpf.scheduler_table[0];
    }

    switch (priority) {
        case PROCESS_PRIORITY_LOW:
        case PROCESS_PRIORITY_MEDIUM:
            mpscq_push(&scheduler->ready_m_and_l_queue, process);
            break;
        case PROCESS_PRIORITY_HIGH:
            mpscq_push(&scheduler->ready_h_queue, process);
            break;
        case PROCESS_PRIORITY_VERY_HIGH:
            mpscq_push(&scheduler->ready_vh_queue, process);
            break;
        default:
            fprintf(stderr, "Invalid priority %d\n", priority);
            process_destroy(process);
            return;
    }
}
