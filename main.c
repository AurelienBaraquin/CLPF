#include <api/task.h>
#include <api/message.h>
#include <api/base.h>
#include <api/tag.h>
#include <api/memory.h>
#include <api/io.h>
#include <api/monitor.h>
#include <assert.h>
#include <msignal.h>
#include <native_task/supervisor.h>

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
    Pid sup = SPAWN0_PRIO(__supervisor__, PROCESS_PRIORITY_MEDIUM);
    int count = 3;
    Pid children[3] = {-1, -1, -1};

    for (int i = 0; i < count; ++i) {
        Funcall_t taskcall;
        taskcall.func = child;
        taskcall.argc = 1;
        taskcall.args[0] = (uintptr_t)i;
        SupChildSpec spec = {
            .magic = MAGIC_SUP_CHILD,
            .taskcall = &taskcall,
            .priority = PROCESS_PRIORITY_MEDIUM,
            .restart = RES_ONE_FOR_ONE,
        };
        Pid *pid = REQUEST(sup, &spec, sizeof(SupChildSpec), MAKE_STR_TAG("supervisor::spawn"));
        if (pid) {
            children[i] = *pid;
            FREE(pid);
        } else {
            PRINT("Failed to spawn child process\n");
            return;
        }
    }

    for (int i = 0; i < count; ++i) {
        if (children[i] == -1) {
            PRINT("Child %d: Failed to spawn\n", i);
            continue;
        }
        SEND(children[i], &i, sizeof(int), NO_TAG);
    }

    for (int i = 0; i < count; ++i) {
        YIELD();
    }

    exit_in_progress = true;

    EXIT();
}

CLPF_ENTRY(task)
