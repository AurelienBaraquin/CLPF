#include <api/task.h>
#include <api/run.h>
#include <api/message.h>
#include <api/base.h>
#include <stdio.h>
#include <string.h>

TASK(other_task, int a) {
    int i = 10;
    while (i --> 0) {
        SEND(PARENT(), &a, sizeof(int));

        char *msg = RECEIVE(NULL, NULL);

        if (msg) {
            printf("Pong: %s\n", msg);
            free(msg);
        } else {
            printf("No message received\n");
        }
    }
    EXIT();
}

TASK(task, int argc, char *argv[]) {
    Pid pid = SPAWN(other_task, 1, 50);
    printf("Spawned other_task with pid: %ld\n", pid);
    char *msg_ = "Hello from task!";

    int i = 10;
    while (i --> 0) {
        int *msg = RECEIVE(NULL, NULL);

        if (msg) {
            printf("Ping: %d\n", *msg);
            free(msg);
        } else {
            printf("No message received in : %ld\n", self->pid);
        }

        SEND(pid, msg_, strlen(msg_) + 1);
    }

    EXIT();
}

CLPF_ENTRY(task)
