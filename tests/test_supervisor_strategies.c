#include "test_helpers.h"
#include <api/supervisor.h>
#include <api/monitor.h>
#include <api/timer.h>
#include <api/link.h>

#define MEMBERS 3

typedef struct {
    int index;
    Pid pid;
} Started;

// Reports its start, then crashes when told to
TASK(member, Pid report_to, int index) {
    Started started = { index, self->pid };
    SEND(report_to, &started, sizeof started, MAKE_STR_TAG("started"));
    MSG_FREE(RECEIVE(MAKE_STR_TAG("crash")));
    EXIT_ERROR();
}

static Started next_start(Process *self) {
    Message *msg = RECEIVE(MAKE_STR_TAG("started"));
    Started started = *(Started *)msg->payload;
    MSG_FREE(msg);
    return started;
}

static unsigned int down_reason(Process *self, Pid pid) {
    Message *msg = RECEIVE(MAKE_DOWN_TAG(pid));
    unsigned int reason = ((DownInfo *)msg->payload)->reason;
    MSG_FREE(msg);
    return reason;
}

// Children run on several schedulers: their reports arrive in any order
static int restarted_mask(Process *self, int count, Pid old_pids[MEMBERS]) {
    int mask = 0;
    for (int i = 0; i < count; ++i) {
        Started started = next_start(self);
        if (started.pid != old_pids[started.index])
            mask |= 1 << started.index;
    }
    return mask;
}

static void start_members(Process *self, Pid sup, Pid pids[MEMBERS]) {
    for (int i = 0; i < MEMBERS; ++i) {
        SUP_ADD_CHILD(sup, RES_PERMANENT, member, "member", 2, self->pid, i);
        pids[i] = next_start(self).pid;
    }
}

TASK(entry, int argc, char *argv[]) {
    (void)argc;
    (void)argv;
    Pid pids[MEMBERS];

    // ONE_FOR_ALL: one crash restarts every child
    Pid all = SPAWN_SUPERVISOR("all", SUP_ONE_FOR_ALL, 1000, 5);
    start_members(self, all, pids);
    MONITOR(pids[0]);
    MONITOR(pids[2]);
    SEND(pids[1], NULL, 0, MAKE_STR_TAG("crash"));
    TEST_ASSERT(down_reason(self, pids[0]) == EXIT_REASON_SHUTDOWN);
    TEST_ASSERT(down_reason(self, pids[2]) == EXIT_REASON_SHUTDOWN);
    TEST_ASSERT(restarted_mask(self, MEMBERS, pids) == 0x7);

    // REST_FOR_ONE: the crashed child and the ones started after it restart
    Pid rest = SPAWN_SUPERVISOR("rest", SUP_REST_FOR_ONE, 1000, 5);
    start_members(self, rest, pids);
    MONITOR(pids[0]);
    MONITOR(pids[2]);
    SEND(pids[1], NULL, 0, MAKE_STR_TAG("crash"));
    TEST_ASSERT(down_reason(self, pids[2]) == EXIT_REASON_SHUTDOWN);
    TEST_ASSERT(restarted_mask(self, 2, pids) == 0x6);
    SLEEP(20);
    TEST_ASSERT(FETCH(MAKE_DOWN_TAG(pids[0])) == NULL); // untouched
    TEST_ASSERT(FETCH(MAKE_STR_TAG("started")) == NULL);

    // A supervisor that is killed takes its linked children with it
    Pid doomed = SPAWN_SUPERVISOR("doomed", SUP_ONE_FOR_ONE, 1000, 5);
    start_members(self, doomed, pids);
    MONITOR(pids[0]);
    KILL(doomed);
    TEST_ASSERT(down_reason(self, pids[0]) == EXIT_REASON_KILLED);

    TEST_PASS();
    EXIT_NORMAL();
}

CLPF_TEST_MAIN(entry)
