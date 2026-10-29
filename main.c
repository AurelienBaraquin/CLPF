#include <api/task.h>
#include <api/message.h>
#include <api/base.h>
#include <api/io.h>
#include <api/supervisor.h>

#include <flags_internal.h>

#define MAX_RESTARTS 1

// Crashes right away: the supervisor restarts it MAX_RESTARTS times, then gives up
TASK(child, Pid report_to) {
    PRINT("Hello from %s\n", self->__name);
    SEND(report_to, NULL, 0, MAKE_STR_TAG("started"));
    EXIT_ERROR();
}

TASK(task, int argc, char *argv[]) {
    (void)argc;
    (void)argv;

    Pid sup = SPAWN_SUPERVISOR("sup", SUP_ONE_FOR_ONE, 0, MAX_RESTARTS);

    Pid pid = SUP_ADD_CHILD(sup, RES_PERMANENT, child, "my_child", 1, self->pid);
    if (pid == INVALID_PID) {
        PRINT("Failed to spawn the child process\n");
        EXIT_ERROR();
    }

    for (int i = 0; i < 1 + MAX_RESTARTS; ++i)
        RECEIVE(MAKE_STR_TAG("started"));

    exit_in_progress = true;

    EXIT_NORMAL();
}

CLPF_ENTRY(task)
