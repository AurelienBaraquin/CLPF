#include <api/task.h>
#include <stdio.h>

Message* receive(Process* self, MessageMatchFn match, void* userdata) {
    while (1) {
        Message* msg;

        if (match)
            msg = mailbox_peek_and_match(&self->mailbox, match, userdata);
        else
            msg = mailbox_receive_message(&self->mailbox);

        if (msg) return msg;

        self->state = PROCESS_WAITING;
        yield(self);
    }
}

void send(Process* self, Pid pid, void* data, size_t size) {
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

void other_task(Process *self, int a) {
    printf("other task running, parent pid: %d and pid : %d\n", self->links->other_pid, self->pid);
    send(self, 0, &a, sizeof(int));
    printf("other task finished\n");
    self->state = PROCESS_EXITING;
}

void task(Process *self, int argc, char *argv[]) {
    SPAWN(other_task, 1, 50);
    for (int i = 0; i < argc; i++) {
        printf("arg %d: %s\n", i, argv[i]);
    }
    Message *msg = receive(self, NULL, NULL);
    if (msg) {
        printf("Received message: %d\n", *(int *)msg->payload);
        free(msg->payload);
        free(msg);
    } else {
        printf("No message received\n");
    }
    self->state = PROCESS_EXITING;
}

int main(int argc, char *argv[]) {
    clpf_main(task, argc, argv);
}
