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
#include <stb_sprintf/stb_sprintf.h>

#include <flags_internal.h>

TASK0(child) {
    PRINT("Hello from %s\n", self->__name);

    EXIT_ERROR();
}

TASK(task, int argc, char *argv[]) {
    Pid sup = SPAWN_SUPERVISOR("sup", SUP_ONE_FOR_ONE, 0, 1);
    int count = 3;
    Pid children[3] = {-1, -1, -1};

    for (int i = 0; i < count; ++i) {
        char buffer[64] = {0};
        stbsp_sprintf(buffer, "my_child_%d", i);
        Pid pid = SUP_ADD_CHILD0(sup, buffer, RES_PERMANENT, child);
        if (pid) {
            children[i] = pid;
        } else {
            PRINT("Failed to spawn child process %d\n", i);
            return;
        }
    }

    for (int i = 0; i < 100; ++i) {
        YIELD();
    }

    exit_in_progress = true;

    EXIT_NORMAL();
}

CLPF_ENTRY(task)
