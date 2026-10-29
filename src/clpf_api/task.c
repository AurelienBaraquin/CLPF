#include <clpf_internal.h>
#include <scheduler_manager.h>
#include <stdio.h>

Pid clpf_task_add(Funcall_t *fun, const SpawnOptions *options, Pid parent_pid, Pid monitored_by)
{
    Pid scheduler_index = options->scheduler > 0 && options->scheduler <= clpf.scheduler_count
        ? options->scheduler - 1 : sch_mg_get_less_process_scheduler();
    if (scheduler_index == -1) {
        fprintf(stderr, "No scheduler available\n");
        return INVALID_PID;
    }
    Scheduler *scheduler = &clpf.scheduler_table[scheduler_index];

    int index = clpf_slot_alloc();
    if (index == -1) {
        fprintf(stderr, "No empty process available\n");
        return INVALID_PID;
    }

    // Nobody knows the new pid yet, but the slot lock still orders this
    // initialization before any later use of the slot by other threads
    Process *process = &clpf.process_table[index];
    pthread_mutex_lock(&process->lock);
    Pid pid = PID_MAKE(index, process->generation);
    process->generation++;
    int init_status = process_init(process, fun, options, pid, parent_pid);
    if (init_status == 0) {
        process->scheduler_pid = scheduler->pid;
        if (monitored_by != INVALID_PID)
            monitored_by_add(&process->monitor, monitored_by);
        if (options->link && parent_pid != INVALID_PID)
            add_to_list(&process->linked, parent_pid);
    } else {
        process->pid = INVALID_PID;
    }
    pthread_mutex_unlock(&process->lock);

    if (init_status != 0) {
        clpf_slot_release(index);
        fprintf(stderr, "Failed to initialize process %ld\n", pid);
        return INVALID_PID;
    }

    Process *parent = process_lock(parent_pid);
    if (parent) {
        link_add_child(&parent->links, pid);
        if (options->link)
            add_to_list(&parent->linked, pid);
        process_unlock(parent);
    }

    pthread_mutex_lock(&clpf.table_lock);
    clpf.process_count++;
    if (!options->system)
        clpf.user_process_count++;
    pthread_mutex_unlock(&clpf.table_lock);

    atomic_fetch_add(&scheduler->process_count, 1);
    scheduler_push_shared(scheduler, process);

    return pid;
}

Pid clpf_spawn(Process *self, Funcall_t *fun, SpawnOptions options)
{
    return clpf_task_add(fun, &options, self->pid, options.monitor ? self->pid : INVALID_PID);
}
