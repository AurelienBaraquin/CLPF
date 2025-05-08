#include <api/task.h>
#include <api/run.h>
#include <api/message.h>
#include <api/base.h>
#include <api/tag.h>
#include <api/memory.h>
#include <stdio.h>
#include <string.h>
#include <assert.h>
#include <stb_sprintf/stb_sprintf.h>

TASK0(child) {
    int *msg = RECEIVE(MessageMatchFn_SameTag, MAKE_INT_TAG(self->pid));
    if (msg) {
        // printf("Child %ld received: %d\n", self->pid, *msg);
        // write(1, msg, sizeof(int));
        char buffer[64];
        stbsp_sprintf(buffer, "Child %ld received: %d\n", self->pid, *msg);
        write(1, buffer, strlen(buffer));
        // FREE(msg);
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
        SEND_TAG(children[i], &i, sizeof(int), MAKE_INT_TAG(children[i]));
    }

    EXIT();
}

CLPF_ENTRY(task)
