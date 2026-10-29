#include <clpf_internal.h>
#include <scheduler_manager.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

Pid __clpf_task_add(char *name, Funcall_t *fun, process_priority priority, Pid parent_pid, Pid monitored_by)
{
    int index = clpf_slot_alloc();
    if (index == -1) {
        fprintf(stderr, "No empty process available\n");
        return -1;
    }

    // create the process
    Process *process = &clpf.process_table[index];
    Pid pid = PID_MAKE(index, process->generation);
    process->generation++;

    pthread_mutex_lock(&process->lock);
    int init_status = process_init(process, name, fun, priority, pid, parent_pid);
    if (init_status != 0)
        process->pid = INVALID_PID;
    else if (monitored_by != INVALID_PID)
        monitored_by_add(&process->monitor, monitored_by);
    pthread_mutex_unlock(&process->lock);

    if (init_status != 0) {
        clpf_slot_release(index);
        fprintf(stderr, "Failed to initialize process %ld\n", pid);
        return -1;
    }

    Scheduler *scheduler = NULL;

    Process *parent = process_lock(parent_pid);
    if (parent) {
        link_add_child(&parent->links, pid);
        // a child runs on the scheduler of its parent
        scheduler = &clpf.scheduler_table[parent->scheduler_pid];
        process_unlock(parent);
    } else {
        Pid less_process_scheduler = sch_mg_get_less_process_scheduler();
        if (less_process_scheduler == -1) {
            fprintf(stderr, "No scheduler available\n");
            process_destroy(process);
            clpf_slot_release(index);
            return -1;
        }
        scheduler = &clpf.scheduler_table[less_process_scheduler];
    }

    process->scheduler_pid = scheduler->pid;
    clpf.process_count++;
    atomic_fetch_add(&scheduler->process_count, 1);

    scheduler_push_shared(scheduler, process);

    name_table_insert(clpf.name_table, name, pid);

    return pid;
}

int __clpf_request_task_add(Process *self, char *name, bool need_reply, bool monitor, Funcall_t *fun, process_priority priority, Pid parent_pid) {
    CoreMessage *msg = malloc(sizeof(CoreMessage));
    if (!msg)
        return -1;

    msg->op = CORE_MESSAGE_TYPE_SPAWN;

    msg->sender = self->pid;
    msg->reply_to = need_reply ? self->pid : -1;

    strncpy(msg->data.spawn.name, name, NAME_MAX_LENGTH);
    msg->data.spawn.taskcall = malloc(sizeof(Funcall_t));
    if (!msg->data.spawn.taskcall)
        return -1;
    memcpy(msg->data.spawn.taskcall, fun, sizeof(Funcall_t));

    msg->data.spawn.priority = priority;
    msg->data.spawn.parent_pid = parent_pid;
    msg->data.spawn.monitor = monitor;

    clpf_post(msg);

    return 0;
}
