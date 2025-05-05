#include <api/task.h>
#include <api/run.h>
#include <api/message.h>
#include <stdio.h>

void other_task(Process *self, int a) {
    SEND(self->links->other_pid, &a, sizeof(int));
    self->state = PROCESS_EXITING;
}

void task(Process *self, int argc, char *argv[]) {
    SPAWN(other_task, 1, 50);
    int *msg = receive(self, NULL, NULL);
    if (msg) {
        printf("Received message: %d\n", *msg);
        free(msg);
    } else {
        printf("No message received\n");
    }
    self->state = PROCESS_EXITING;
}

int main(int argc, char *argv[]) {
    clpf_main(task, argc, argv);
}
