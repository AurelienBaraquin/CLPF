#include <api/task.h>
#include <api/run.h>
#include <api/message.h>
#include <api/base.h>
#include <stdio.h>

TASK(other_task, int a) {
    int i = 10;
    while (i-- > 0) {
        SEND(PARENT(), &a, sizeof(int));

        int *msg = RECEIVE(NULL, NULL);

        if (msg) {
            printf("Pong: %d\n", *msg);
            free(msg);
        } else {
            printf("No message received\n");
        }
    }
    EXIT();
}

TASK(task, int argc, char *argv[]) {
    SPAWN(other_task, 1, 50);
    
    int i = 10;
    while (i-- > 0) {
        int *msg = RECEIVE(NULL, NULL);

        if (msg) {
            printf("Ping: %d\n", *msg);
            free(msg);
        } else {
            printf("No message received in : %ld\n", self->pid);
        }

        SEND(CHILD(0), &argc, sizeof(int));
    }

    EXIT();
}

CLPF_ENTRY(task)
