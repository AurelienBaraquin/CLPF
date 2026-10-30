#include <clpf_internal.h>
#include <stdio.h>
#include <api/message.h>
#include <stdlib.h>
#include <string.h>
#include <io_poller.h>
#include <groups.h>
#include <tables.h>

void clpf_message_destroy(CoreMessage *msg) {
    if (msg) {
        free(msg);
    }
}

int clpf_main_update() {
    MPSCQNode *node = mpscq_drain(&clpf.message_queue);
    if (node == NULL)
        return 0;

    int count = 0;

    for (MPSCQNode *n = node; n != NULL; n = n->next) {
        CoreMessage *msg = container_of(n, CoreMessage, node);
        switch (msg->op) {
            case CORE_MESSAGE_TYPE_PROCESS_EXIT: {
                // Only the main thread destroys processes: once found, this one stays alive until destroyed below
                Process *process = process_lock(msg->data.process_exit.pid);
                if (!process)
                    break;
                Pid pid = process->pid;
                Pid parent_pid = process->links.parent;
                bool system = process->system;
                bool entry = process->entry;
                unsigned int exit_reason = process->exit_reason;
                process_unlock(process);

                if (entry) {
                    clpf.shutdown_requested = true;
                    clpf.entry_exit_reason = exit_reason;
                }

                pthread_mutex_lock(&clpf.table_lock);
                // The name may already belong to another process if REGISTER reclaimed it
                if (process->registered_name[0] && name_table_lookup(clpf.name_table, process->registered_name) == pid)
                    name_table_remove(clpf.name_table, process->registered_name);
                clpf.process_count--;
                if (!system)
                    clpf.user_process_count--;
                pthread_mutex_unlock(&clpf.table_lock);

                Process *parent = process_lock(parent_pid);
                if (parent) {
                    link_remove_child(&parent->links, pid);
                    process_unlock(parent);
                }
                io_poller_forget(pid);
                if (process->group_count > 0)
                    groups_forget(pid);
                if (process->table_count > 0)
                    tables_forget(pid);
                process_destroy(process);
                clpf_slot_release(PID_INDEX(pid));
                break;
            }
            default: break;
        }
        ++count;
    }

    for (MPSCQNode *n = node; n != NULL;) {
        MPSCQNode *next = n->next;
        clpf_message_destroy(container_of(n, CoreMessage, node));
        n = next;
    }

    return count;
}
