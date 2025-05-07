#include <api/task.h>
#include <api/run.h>
#include <api/message.h>
#include <api/base.h>
#include <api/tag.h>
#include <api/memory.h>
#include <stdio.h>
#include <string.h>

TASK0(child) {
    int *msg = RECEIVE(NULL, NULL);
    if (msg) {
        printf("Child %ld received: %d\n", self->pid, *msg);
        FREE(msg);
    }
    EXIT();
}

TASK(task, int argc, char *argv[]) {
    const int count = 10;
    Pid children[count];

    for (int i = 0; i < count; ++i) {
        children[i] = SPAWN0(child);
    }

    for (int i = 0; i < count; ++i) {
        SEND(children[i], &i, sizeof(int));
    }

    EXIT();
}

CLPF_ENTRY(task)
