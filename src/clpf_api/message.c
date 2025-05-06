#include <api/message.h>
#include <stdio.h>

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
            free(msg);
            return payload;
        }

        self->state = PROCESS_WAITING;
        yield(self);
    }
}

void send(Pid pid, void* data, size_t size, Tag tag) {
    if (pid == -1) {
        fprintf(stderr, "Invalid PID: %ld\n", pid);
        return;
    }

    Process* other_process = &clpf.process_table[pid];
    if (!other_process) {
        fprintf(stderr, "Process %ld not found\n", pid);
        return;
    }
    mailbox_send_message(&other_process->mailbox, data, size, tag);
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
