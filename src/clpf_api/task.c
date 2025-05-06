#include <clpf_internal.h>
#include <scheduler_manager.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

Pid __clpf_task_add(Funcall_t *fun, process_priority priority, Pid parent_pid)
{
    // find an empty process (when id is 0)
    Pid empty_process = -1;
    for (int i = 0; i < MAX_PROCESSES; i++) {
        if (clpf.process_table[i].pid == -1) {
            empty_process = i;
            break;
        }
    }

    if (empty_process == -1) {
        fprintf(stderr, "No empty process available\n");
        return -1;
    }

    // create the process
    Process *process = &clpf.process_table[empty_process];
    if (process_init(process, fun, priority, empty_process, parent_pid) != 0) {
        fprintf(stderr, "Failed to initialize process %ld\n", empty_process);
        return -1;
    }

    Scheduler *scheduler = NULL;

    if (parent_pid == -1) {
        Pid less_process_scheduler = sch_mg_get_less_process_scheduler();
        if (less_process_scheduler == -1) {
            fprintf(stderr, "No scheduler available\n");
            process_destroy(process);
            return -1;
        }
        scheduler = &clpf.scheduler_table[less_process_scheduler];
    } else {
        // take the scheduler of the parent process
        // TODO : if the scheduler is already very busy, we should take the less busy scheduler
        scheduler = &clpf.scheduler_table[clpf.process_table[parent_pid].scheduler_pid];
    }

    if (!scheduler) {
        // take the first scheduler
        scheduler = &clpf.scheduler_table[0];
    }

    process->scheduler_pid = scheduler->pid;
    clpf.process_count++;
    atomic_fetch_add(&scheduler->process_count, 1);

    scheduler_push_shared(scheduler, process);

    return process->pid;
}

int __clpf_request_task_add(Process *self, bool need_reply, Funcall_t *fun, process_priority priority, Pid parent_pid) {
    CoreMessage *msg = malloc(sizeof(CoreMessage));
    if (!msg)
        return -1;

    msg->op = CORE_MESSAGE_TYPE_SPAWN;

    msg->sender = self->pid;
    msg->reply_to = need_reply ? self->pid : -1;

    msg->data.spawn.taskcall = malloc(sizeof(Funcall_t));
    if (!msg->data.spawn.taskcall)
        return -1;
    memcpy(msg->data.spawn.taskcall, fun, sizeof(Funcall_t));

    msg->data.spawn.priority = priority;
    msg->data.spawn.parent_pid = parent_pid;

    mpscq_push(&clpf.message_queue, msg);

    return 0;
}
