#include <api/task.h>
#include <api/message.h>
#include <api/base.h>
#include <api/tag.h>
#include <api/memory.h>
#include <api/io.h>
#include <api/monitor.h>
#include <assert.h>
#include <msignal.h>
#include <api/supervisor.h>

#include <flags_internal.h>

int DEF(add, int a, int b) {
    return a + b;
}

TASK0(child) {
    write(STDOUT_FILENO, "Child process started\n", 22);
    int *msg = RECEIVE(NO_TAG);
    if (msg) {
        PRINT("Child %d: Hello from child process %d\n", CALL(add, 10, *msg), self->pid);
        FREE(msg);
    }

    EXIT();
}

TASK(task, int argc, char *argv[]) {
    Pid sup = SPAWN_SUPERVISOR(SUP_ONE_FOR_ONE);
    int count = 3;
    Pid children[3] = {-1, -1, -1};

    for (int i = 0; i < count; ++i) {
        Pid pid = SUP_ADD_CHILD0(sup, RES_PERMANENT, child);
        if (pid) {
            children[i] = pid;
        } else {
            PRINT("Failed to spawn child process %d\n", i);
            return;
        }
    }

    for (int i = 0; i < count; ++i) {
        if (children[i] == -1) {
            PRINT("Child %d not spawned\n", i);
            continue;
        }
        SEND(children[i], &i, sizeof(int), NO_TAG);
    }

    for (int i = 0; i < count * 3; ++i) {
        YIELD();
    }

    exit_in_progress = true;

    EXIT();
}

CLPF_ENTRY(task)
