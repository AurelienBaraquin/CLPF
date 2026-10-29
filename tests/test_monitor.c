#include "test_helpers.h"
#include <api/task.h>
#include <api/message.h>
#include <api/monitor.h>

#define MAX_WAIT_YIELDS 100000

typedef struct {
    int downs;
    Pid from;
    unsigned int reason;
} DownLog;

static void on_down(Process *self, Pid from, unsigned int reason, DownLog *log) {
    (void)self;
    log->downs++;
    log->from = from;
    log->reason = reason;
}

TASK0(wait_then_exit) {
    MSG_FREE(RECEIVE(MAKE_STR_TAG("go")));
    EXIT_NORMAL();
}

TASK(entry, int argc, char *argv[]) {
    (void)argc;
    (void)argv;

    DownLog log = {0};
    MONITOR_INIT(on_down, &log);

    Pid child = SPAWN0(wait_then_exit);
    TEST_ASSERT(child != INVALID_PID);
    TEST_ASSERT(NB_CHILDREN() == 1);
    TEST_ASSERT(CHILD(0) == child);

    // A monitored process exiting normally sends one DOWN
    MONITOR_ADD(child);
    SEND(child, NULL, 0, MAKE_STR_TAG("go"));
    for (int i = 0; log.downs == 0 && i < MAX_WAIT_YIELDS; ++i)
        YIELD();
    TEST_ASSERT(log.downs == 1);
    TEST_ASSERT(log.from == child);
    TEST_ASSERT(log.reason == EXIT_REASON_NORMAL);

    // Once destroyed, the child leaves its parent's children list
    for (int i = 0; NB_CHILDREN() != 0 && i < MAX_WAIT_YIELDS; ++i)
        YIELD();
    TEST_ASSERT(NB_CHILDREN() == 0);

    // Monitoring a process that is already gone reports it right away
    MONITOR_ADD(child);
    for (int i = 0; log.downs == 1 && i < MAX_WAIT_YIELDS; ++i)
        YIELD();
    TEST_ASSERT(log.downs == 2);
    TEST_ASSERT(log.from == child);
    TEST_ASSERT(log.reason == EXIT_REASON_NOPROC);

    TEST_PASS();
    EXIT_NORMAL();
}

CLPF_TEST_MAIN(entry)
