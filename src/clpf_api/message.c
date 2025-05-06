#include <api/message.h>
#include <stdio.h>
#include <string.h>

void* receive(Process* self, MessageMatchFn match, void* userdata) {
    while (1) {
        mailbox_transfer_inbox_to_seen(&self->mailbox);

        Message* msg;

        if (match)
            msg = mailbox_peek_and_match_seen(&self->mailbox, match, userdata);
        else
            msg = mailbox_receive_seen(&self->mailbox);

        if (msg) {
            void *payload = msg->payload;
            safe_heap_free((safe_heap_t *)msg->heap, msg);
            return payload;
        }

        self->state = PROCESS_WAITING;
        yield(self);
    }
}

void send(Process *self, Pid pid, void* data, size_t size, Tag tag) {
    if (pid == -1) {
        fprintf(stderr, "Invalid PID: %ld\n", pid);
        return;
    }

    Process* other_process = &clpf.process_table[pid];
    if (!other_process) {
        fprintf(stderr, "Process %ld not found\n", pid);
        return;
    }

    if (other_process)
        mailbox_send_message(&other_process->mailbox, (heap_t*)&other_process->heap, data, size, tag);
    else
        mailbox_send_message(&other_process->mailbox, NULL, data, size, tag);

    if (other_process->state == PROCESS_WAITING) {
        other_process->state = PROCESS_READY;
    }
}



/*
    Some matching functions for the messages
*/

// This function matches messages with the same tag
bool MessageMatchFn_SameTag(Message *msg, void *userdata) {
    Tag tag = (Tag)(uintptr_t)userdata;
    return msg->tag == tag;
}

// This function matches messages with the same data string
bool MessageMatchFn_SameDataString(Message *msg, void *userdata) {
    if (!msg || !userdata)
        return false;

    return strcmp((char *)msg->payload, (char *)userdata) == 0;
}
