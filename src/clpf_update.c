#include <clpf_internal.h>
#include <clpf_const.h>
#include <stdio.h>
#include <api/message.h>
#include <stdlib.h>
#include <string.h>

void __clpf_message_destroy(CoreMessage* msg)
{
    if (msg) {
        switch (msg->op) {
            case CORE_MESSAGE_TYPE_SPAWN:
                free(msg->data.spawn.taskcall);
                break;
            default:
                break;
        }
        free(msg);
    }
}

int __clpf_main_update()
{
    MPSCQNode* node = mpscq_drain(&clpf.message_queue);
    if (node == NULL) return 0;

    int count = 0;

    for (MPSCQNode* n = node; n != NULL; n = n->next) {
        CoreMessage* msg = container_of(n, CoreMessage, node);
        switch (msg->op) {
            case CORE_MESSAGE_TYPE_SPAWN:
                Pid new_pid = __clpf_task_add(msg->data.spawn.name, msg->data.spawn.taskcall, msg->data.spawn.priority, msg->data.spawn.parent_pid,
                                              msg->data.spawn.monitor ? msg->sender : INVALID_PID);
                if (msg->reply_to != -1)
                    send(NULL, msg->reply_to, &new_pid, sizeof(Pid), CORE_MESSAGE_TYPE_SPAWN_TAG);
                break;

            case CORE_MESSAGE_TYPE_NAME_REGISTER: {
                Process *process = process_get(msg->data.name_register.pid);
                if (process && name_table_insert(clpf.name_table, msg->data.name_register.name, process->pid) == 0) {
                    memset(process->__name, 0, NAME_MAX_LENGTH);
                    strcpy(process->__name, msg->data.name_register.name);
                }
                break;
            }
            case CORE_MESSAGE_TYPE_NAME_UNREGISTER: {
                Process *registered = process_get(name_table_lookup(clpf.name_table, msg->data.name_unregister.name));
                if (registered)
                    memset(registered->__name, 0, NAME_MAX_LENGTH);
                name_table_remove(clpf.name_table, msg->data.name_unregister.name);
                break;
            }
            case CORE_MESSAGE_TYPE_NAME_LOOKUP:
                Pid pid = name_table_lookup(clpf.name_table, msg->data.name_lookup.name);
                if (msg->reply_to == -1) {
                    fprintf(stderr, "[ERROR] No reply_to for name lookup\n");
                    break;
                }
                send(NULL, msg->reply_to, &pid, sizeof(Pid), CORE_MESSAGE_TYPE_NAME_LOOKUP_TAG);
                break;

            case CORE_MESSAGE_TYPE_PROCESS_EXIT: {
                Process *process = process_get(msg->data.process_exit.pid);
                if (!process)
                    break;
                // The name may already belong to a restarted process
                if (name_table_lookup(clpf.name_table, process->__name) == process->pid)
                    name_table_remove(clpf.name_table, process->__name);
                Process *parent = process_lock(process->links.parent);
                if (parent) {
                    link_remove_child(&parent->links, process->pid);
                    process_unlock(parent);
                }
                process_destroy(process);
                clpf.process_count--;
                break;
            }
            default:
                break;
        }
        ++count;
    }

    for (MPSCQNode* n = node; n != NULL; ) {
        MPSCQNode* next = n->next;
        __clpf_message_destroy(container_of(n, CoreMessage, node));
        n = next;
    }

    return count;
}
