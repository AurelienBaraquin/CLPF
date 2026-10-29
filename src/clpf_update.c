#include <clpf_internal.h>
#include <clpf_const.h>
#include <stdio.h>
#include <api/message.h>
#include <stdlib.h>
#include <string.h>

void clpf_message_destroy(CoreMessage* msg)
{
    if (msg) {
        free(msg);
    }
}

int clpf_main_update()
{
    MPSCQNode* node = mpscq_drain(&clpf.message_queue);
    if (node == NULL) return 0;

    int count = 0;

    for (MPSCQNode* n = node; n != NULL; n = n->next) {
        CoreMessage* msg = container_of(n, CoreMessage, node);
        switch (msg->op) {
            case CORE_MESSAGE_TYPE_NAME_REGISTER: {
                Process *process = process_lock(msg->data.name_register.pid);
                if (!process)
                    break;
                pthread_mutex_lock(&clpf.table_lock);
                if (name_table_insert(clpf.name_table, msg->data.name_register.name, process->pid) == 0) {
                    memset(process->name, 0, NAME_MAX_LENGTH);
                    strcpy(process->name, msg->data.name_register.name);
                }
                pthread_mutex_unlock(&clpf.table_lock);
                process_unlock(process);
                break;
            }
            case CORE_MESSAGE_TYPE_NAME_UNREGISTER: {
                pthread_mutex_lock(&clpf.table_lock);
                Pid registered_pid = name_table_lookup(clpf.name_table, msg->data.name_unregister.name);
                name_table_remove(clpf.name_table, msg->data.name_unregister.name);
                pthread_mutex_unlock(&clpf.table_lock);

                Process *registered = process_lock(registered_pid);
                if (registered) {
                    memset(registered->name, 0, NAME_MAX_LENGTH);
                    process_unlock(registered);
                }
                break;
            }
            case CORE_MESSAGE_TYPE_NAME_LOOKUP:
                pthread_mutex_lock(&clpf.table_lock);
                Pid pid = name_table_lookup(clpf.name_table, msg->data.name_lookup.name);
                pthread_mutex_unlock(&clpf.table_lock);
                if (msg->reply_to == -1) {
                    fprintf(stderr, "[ERROR] No reply_to for name lookup\n");
                    break;
                }
                clpf_send(NULL, msg->reply_to, &pid, sizeof(Pid), CORE_MESSAGE_TYPE_NAME_LOOKUP_TAG);
                break;

            case CORE_MESSAGE_TYPE_PROCESS_EXIT: {
                // Only the main thread destroys processes: once found, this one stays alive until destroyed below
                Process *process = process_lock(msg->data.process_exit.pid);
                if (!process)
                    break;
                Pid pid = process->pid;
                Pid parent_pid = process->links.parent;
                process_unlock(process);

                pthread_mutex_lock(&clpf.table_lock);
                // The name may already belong to a restarted process
                if (name_table_lookup(clpf.name_table, process->name) == pid)
                    name_table_remove(clpf.name_table, process->name);
                clpf.process_count--;
                pthread_mutex_unlock(&clpf.table_lock);

                Process *parent = process_lock(parent_pid);
                if (parent) {
                    link_remove_child(&parent->links, pid);
                    process_unlock(parent);
                }
                process_destroy(process);
                clpf_slot_release(PID_INDEX(pid));
                break;
            }
            default:
                break;
        }
        ++count;
    }

    for (MPSCQNode* n = node; n != NULL; ) {
        MPSCQNode* next = n->next;
        clpf_message_destroy(container_of(n, CoreMessage, node));
        n = next;
    }

    return count;
}
