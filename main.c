#include <api/task.h>
#include <api/run.h>
#include <api/message.h>
#include <api/base.h>
#include <api/tag.h>
#include <api/memory.h>
#include <api/io.h>
#include <assert.h>

TASK0(child) {
    int *msg = RECEIVE(NO_TAG);
    if (msg) {
        PRINT("Child %d: Hello from child process %d\n", *msg, self->pid);
        FREE(msg);
    }
    EXIT();
}

TASK(task, int argc, char *argv[]) {
    int count = 0;
    Pid children[100];

    Pid io = WHEREIS("server::io");
    if (io == -1) {
        fprintf(stderr, "IO server not found\n");
        EXIT();
    }
    char *r = __getline(self);
    assert(r);

    count = atoi(r);

    FREE(r);

    for (int i = 0; i < count; ++i) {
        children[i] = SPAWN0(child);
    }

    for (int i = 0; i < count; ++i) {
        SEND(children[i], &i, sizeof(int), NO_TAG);
    }

    EXIT();
}

CLPF_ENTRY(task)
