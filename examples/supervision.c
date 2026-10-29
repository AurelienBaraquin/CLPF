// A ONE_FOR_ALL supervisor: when one worker crashes, both are restarted.
// Past its restart intensity the supervisor gives up, and its monitor sees it.
#include <api/task.h>
#include <api/message.h>
#include <api/monitor.h>
#include <api/supervisor.h>
#include <api/io.h>

TASK(worker, Pid boss, int id) {
    PRINT("worker %d started (pid %ld)\n", id, self->pid);
    SEND(boss, &id, sizeof id, MAKE_STR_TAG("up"));
    MSG_FREE(RECEIVE(MAKE_STR_TAG("crash")));
    EXIT_ERROR();
}

static Pid wait_up(Process *self) {
    Message *msg = RECEIVE(MAKE_STR_TAG("up"));
    Pid pid = msg->sender_pid;
    MSG_FREE(msg);
    return pid;
}

TASK(entry, int argc, char *argv[]) {
    (void)argc;
    (void)argv;

    Pid sup = SPAWN_SUPERVISOR("pool", SUP_ONE_FOR_ALL, 1000 /* ms */, 2 /* restarts */);
    MONITOR(sup);
    SUP_ADD_CHILD(sup, RES_PERMANENT, worker, "worker", 2, self->pid, 1);
    SUP_ADD_CHILD(sup, RES_PERMANENT, worker, "worker", 2, self->pid, 2);
    Pid first = wait_up(self);
    wait_up(self);

    // Each crash restarts both workers; the third one is one too many
    for (int crash = 1; crash <= 3; ++crash) {
        PRINT("crashing a worker (%d)\n", crash);
        SEND(first, NULL, 0, MAKE_STR_TAG("crash"));
        if (crash < 3) {
            first = wait_up(self);
            wait_up(self);
        }
    }

    Message *down = RECEIVE(MAKE_DOWN_TAG(sup));
    PRINT("supervisor gave up (reason %u)\n", ((DownInfo *)down->payload)->reason);
    MSG_FREE(down);
    EXIT_NORMAL();
}

CLPF_ENTRY(entry)
