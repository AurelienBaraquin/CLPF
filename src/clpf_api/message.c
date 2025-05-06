#include <api/message.h>
#include <stdio.h>

void* receive(Process* self, MessageMatchFn match, void* userdata) {
    while (1) {
        Message* msg;

        if (match)
            msg = mailbox_peek_and_match(&self->mailbox, match, userdata);
        else
            msg = mailbox_receive_message(&self->mailbox);

        if (msg) {
            void *payload = msg->payload;
            free(msg);
            return payload;
        }

        self->state = PROCESS_WAITING;
        yield(self);
    }
}

void send(Process* self, Pid pid, void* data, size_t size) {
    if (pid == -1) {
        fprintf(stderr, "Invalid PID: %ld\n", pid);
        return;
    }

    Process* other_process = &clpf.process_table[pid];
    if (!other_process) {
        fprintf(stderr, "Process %ld not found\n", pid);
        return;
    }
    mailbox_send_message(&other_process->mailbox, data, size);
    if (other_process->state == PROCESS_WAITING) {
        other_process->state = PROCESS_READY;
    }
}
