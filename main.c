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
    YIELD();
    YIELD();
    int a = 10;
    int *msg = &a;
    if (msg) {
        PRINT("Child %d: Hello from child process %d\n", CALL(add, 10, *msg), self->pid);
    }

    EXIT_ERROR();
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

    for (int i = 0; i < count * 100; ++i) {
        YIELD();
    }

    exit_in_progress = true;

    EXIT_NORMAL();
}

CLPF_ENTRY(task)
