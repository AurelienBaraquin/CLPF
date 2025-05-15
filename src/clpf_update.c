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
        CoreMessage* msg = (CoreMessage*)n->data;
        switch (msg->op) {
            case CORE_MESSAGE_TYPE_SPAWN:
                Pid new_pid = __clpf_task_add(msg->data.spawn.name, msg->data.spawn.taskcall, msg->data.spawn.priority, msg->data.spawn.parent_pid);
                if (msg->reply_to != -1)
                    send(NULL, msg->reply_to, &new_pid, sizeof(Pid), CORE_MESSAGE_TYPE_SPAWN_TAG);
                break;

            case CORE_MESSAGE_TYPE_NAME_REGISTER:
                if (name_table_insert(clpf.name_table, msg->data.name_register.name, msg->data.name_register.pid) == 0) {
                    memset(clpf.process_table[msg->data.name_register.pid].__name, 0, NAME_MAX_LENGTH);
                    strcpy(clpf.process_table[msg->data.name_register.pid].__name, msg->data.name_register.name);
                }
                break;
            case CORE_MESSAGE_TYPE_NAME_UNREGISTER:
                memset(clpf.process_table[name_table_lookup(clpf.name_table, msg->data.name_unregister.name)].__name, 0, NAME_MAX_LENGTH);
                name_table_remove(clpf.name_table, msg->data.name_unregister.name);
                break;
            case CORE_MESSAGE_TYPE_NAME_LOOKUP:
                Pid pid = name_table_lookup(clpf.name_table, msg->data.name_lookup.name);
                if (msg->reply_to == -1) {
                    fprintf(stderr, "[ERROR] No reply_to for name lookup\n");
                    break;
                }
                send(NULL, msg->reply_to, &pid, sizeof(Pid), CORE_MESSAGE_TYPE_NAME_LOOKUP_TAG);
                break;

            case CORE_MESSAGE_TYPE_PROCESS_EXIT:
                if (msg->data.process_exit.pid != -1) {
                    name_table_remove(clpf.name_table, clpf.process_table[msg->data.process_exit.pid].__name);
                    process_destroy(&clpf.process_table[msg->data.process_exit.pid]);
                    clpf.process_table[msg->data.process_exit.pid].pid = -1;
                    clpf.process_count--;
                }
                break;
            default:
                break;
        }
        ++count;
    }

    for (MPSCQNode* n = node; n != NULL; ) {
        MPSCQNode* next = n->next;
        __clpf_message_destroy((CoreMessage*)n->data);
        free(n);
        n = next;
    }

    return count;
}
