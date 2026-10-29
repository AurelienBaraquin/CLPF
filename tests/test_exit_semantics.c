#include "test_helpers.h"
#include <api/task.h>
#include <api/message.h>
#include <api/monitor.h>

#define MAX_WAIT_YIELDS 100000

typedef struct {
    int downs;
    unsigned int reason;
} DownLog;

static void on_down(Process *self, Pid from, unsigned int reason, DownLog *log) {
    (void)self;
    (void)from;
    log->downs++;
    log->reason = reason;
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

static void wait_for_downs(Process *self, DownLog *log, int expected) {
    for (int i = 0; log->downs < expected && i < MAX_WAIT_YIELDS; ++i)
        YIELD();
}

TASK(entry, int argc, char *argv[]) {
    (void)argc;
    (void)argv;

    DownLog log = {0};
    MONITOR_INIT(on_down, &log);

    Pid early = SPAWN(exit_early, 1, self->pid);
    MONITOR_ADD(early);
    SEND(early, NULL, 0, MAKE_STR_TAG("go"));
    MSG_FREE(RECEIVE(MAKE_STR_TAG("before")));
    wait_for_downs(self, &log, 1);
    TEST_ASSERT(log.downs == 1);
    TEST_ASSERT(log.reason == EXIT_REASON_ERROR);
    TEST_ASSERT(FETCH(MAKE_STR_TAG("after")) == NULL);

    Pid plain = SPAWN0(just_return);
    MONITOR_ADD(plain);
    SEND(plain, NULL, 0, MAKE_STR_TAG("go"));
    wait_for_downs(self, &log, 2);
    TEST_ASSERT(log.downs == 2);
    TEST_ASSERT(log.reason == EXIT_REASON_NORMAL);

    TEST_PASS();
    EXIT_NORMAL();
}

CLPF_TEST_MAIN(entry)
