#include <api/task.h>
#include <api/run.h>
#include <api/message.h>
#include <api/base.h>
#include <stdio.h>

TASK(other_task, int a) {
    SEND(PARENT(), &a, sizeof(int));
    EXIT();
}

TASK(task, int argc, char *argv[]) {
    SPAWN(other_task, 1, 50);

    int *msg = receive(self, NULL, NULL);

    if (msg) {
        printf("Received message: %d\n", *msg);
        free(msg);
    } else {
        printf("No message received\n");
    }

    EXIT();
}

CLPF_ENTRY(task)
