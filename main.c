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
    int *msg = RECEIVE(NO_TAG);
    if (msg) {
        char buffer[64];
        stbsp_sprintf(buffer, "Child %ld received: %d\n", self->pid, *msg);
        Pid io = WHEREIS("server::io");
        SEND_TAG(io, buffer, strlen(buffer), MAKE_STR_TAG("server::io::print"));
        FREE(msg);
    }
    EXIT();
}

TASK(task, int argc, char *argv[]) {
    int count = 2;
    Pid children[count];

    Pid io = WHEREIS("server::io");
    if (io == -1) {
        fprintf(stderr, "IO server not found\n");
        EXIT();
    }
    SEND_TAG(io, NULL, 0, MAKE_STR_TAG("server::io::getline"));
    char *r = RECEIVE(MAKE_STR_TAG("server::io::getline"));

    count = atoi(r);

    for (int i = 0; i < count; ++i) {
        children[i] = SPAWN0(child);
    }

    for (int i = 0; i < count; ++i) {
        SEND_TAG(children[i], &i, sizeof(int), NO_TAG);
    }

    EXIT();
}

CLPF_ENTRY(task)
