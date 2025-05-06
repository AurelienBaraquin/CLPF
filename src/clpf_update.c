#include <clpf_internal.h>

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
                Pid new_pid = __clpf_task_add(msg->data.spawn.taskcall, msg->data.spawn.priority, msg->data.spawn.parent_pid);
                if (msg->reply_to != -1) {
                    mailbox_send_message(&clpf.process_table[msg->reply_to].mailbox,
                                         &new_pid, sizeof(Pid));
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
