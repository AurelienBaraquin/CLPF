#include "test_helpers.h"
#include <api/task.h>
#include <api/message.h>
#include <api/monitor.h>

#define MAX_WAIT_YIELDS 100000

TASK0(wait_then_exit) {
    MSG_FREE(RECEIVE(MAKE_STR_TAG("go")));
    EXIT_NORMAL();
}

static DownInfo take_down(Process *self, Pid from) {
    Message *msg = RECEIVE(MAKE_DOWN_TAG(from));
    DownInfo down = *(DownInfo *)msg->payload;
    MSG_FREE(msg);
    return down;
}

TASK(entry, int argc, char *argv[]) {
    (void)argc;
    (void)argv;

    Pid child = SPAWN0(wait_then_exit);
    TEST_ASSERT(child != INVALID_PID);
    TEST_ASSERT(NB_CHILDREN() == 1);
    TEST_ASSERT(CHILD(0) == child);

    // A monitored process exiting normally sends one DOWN message, which wakes a blocked RECEIVE
    MONITOR(child);
    SEND(child, NULL, 0, MAKE_STR_TAG("go"));
    DownInfo down = take_down(self, child);
    TEST_ASSERT(down.from == child);
    TEST_ASSERT(down.reason == EXIT_REASON_NORMAL);

    // Once destroyed, the child leaves its parent's children list
    for (int i = 0; NB_CHILDREN() != 0 && i < MAX_WAIT_YIELDS; ++i)
        YIELD();
    TEST_ASSERT(NB_CHILDREN() == 0);

    // Monitoring a process that is already gone reports it right away
    MONITOR(child);
    down = take_down(self, child);
    TEST_ASSERT(down.reason == EXIT_REASON_NOPROC);

    // A DEMONITORed process exits silently
    Pid other = SPAWN0(wait_then_exit);
    MONITOR(other);
    DEMONITOR(other);
    SEND(other, NULL, 0, MAKE_STR_TAG("go"));
    for (int i = 0; NB_CHILDREN() != 0 && i < MAX_WAIT_YIELDS; ++i)
        YIELD();
    TEST_ASSERT(FETCH(MAKE_DOWN_TAG(other)) == NULL);

    // RECEIVE_DOWN takes the DOWN of any monitored process
    Pid first = SPAWN0(wait_then_exit);
    MONITOR(first);
    SEND(first, NULL, 0, MAKE_STR_TAG("go"));
    Message *any = RECEIVE_DOWN();
    TEST_ASSERT(((DownInfo *)any->payload)->from == first);
    MSG_FREE(any);

    TEST_PASS();
    EXIT_NORMAL();
}

CLPF_TEST_MAIN(entry)
