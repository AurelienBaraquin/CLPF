#include "test_helpers.h"
#include <api/task.h>
#include <api/message.h>
#include <api/monitor.h>

static unsigned int down_reason(Process *self, Pid from) {
    Message *msg = RECEIVE(MAKE_DOWN_TAG(from));
    unsigned int reason = ((DownInfo *)msg->payload)->reason;
    MSG_FREE(msg);
    return reason;
}

// EXIT_ERROR must stop the process: the second message is never sent
TASK(exit_early, Pid parent) {
    MSG_FREE(RECEIVE(MAKE_STR_TAG("go")));
    SEND(parent, NULL, 0, MAKE_STR_TAG("before"));
    EXIT_ERROR();
    SEND(parent, NULL, 0, MAKE_STR_TAG("after"));
}

// Returning without EXIT is a normal exit
TASK0(just_return) {
    MSG_FREE(RECEIVE(MAKE_STR_TAG("go")));
}

TASK(entry, int argc, char *argv[]) {
    (void)argc;
    (void)argv;

    Pid early = SPAWN(exit_early, 1, self->pid);
    MONITOR(early);
    SEND(early, NULL, 0, MAKE_STR_TAG("go"));
    MSG_FREE(RECEIVE(MAKE_STR_TAG("before")));
    TEST_ASSERT(down_reason(self, early) == EXIT_REASON_ERROR);
    TEST_ASSERT(FETCH(MAKE_STR_TAG("after")) == NULL);

    Pid plain = SPAWN0(just_return);
    MONITOR(plain);
    SEND(plain, NULL, 0, MAKE_STR_TAG("go"));
    TEST_ASSERT(down_reason(self, plain) == EXIT_REASON_NORMAL);

    TEST_PASS();
    EXIT_NORMAL();
}

CLPF_TEST_MAIN(entry)
