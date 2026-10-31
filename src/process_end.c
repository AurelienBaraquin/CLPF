#include <clpf_internal.h>
#include <io_poller.h>
#include <groups.h>
#include <tables.h>

void clpf_process_end(Process *process) {
    // Nothing reaches the process anymore but its slot lock: senders find it
    // exited, and only this thread destroys it
    pthread_mutex_lock(&process->lock);
    Pid pid = process->pid;
    Pid parent_pid = process->links.parent;
    bool system = process->system;
    bool entry = process->entry;
    unsigned int exit_reason = process->exit_reason;
    pthread_mutex_unlock(&process->lock);

    if (entry) {
        atomic_store(&clpf.entry_exit_reason, exit_reason);
        atomic_store(&clpf.shutdown_requested, true);
    }

    // Only REGISTER sets the name, under the table lock, and only on a live process
    if (process->registered_name[0]) {
        pthread_mutex_lock(&clpf.table_lock);
        // The name may already belong to another process if REGISTER reclaimed it
        if (name_table_lookup(clpf.name_table, process->registered_name) == pid)
            name_table_remove(clpf.name_table, process->registered_name);
        pthread_mutex_unlock(&clpf.table_lock);
    }
    if (!system)
        atomic_fetch_sub(&clpf.user_process_count, 1);

    Process *parent = process_lock(parent_pid);
    if (parent) {
        link_remove_child(&parent->links, &process->links);
        process_unlock(parent);
    }
    io_poller_forget(pid);
    if (process->group_count > 0)
        groups_forget(pid);
    if (process->table_count > 0)
        tables_forget(pid);
    process_destroy(process);
    clpf_slot_release(PID_INDEX(pid));
}
