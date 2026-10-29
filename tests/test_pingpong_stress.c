#include "test_helpers.h"
#include <api/task.h>
#include <api/message.h>
#include <api/memory.h>

#define PAIRS       8
#define ROUND_TRIPS 2000

// New processes are spread over the schedulers, so most messages below
// cross threads. A lost wakeup leaves a process waiting forever and the
// test hits its timeout.
TASK0(echo) {
    for (int i = 0; i < ROUND_TRIPS; ++i) {
        Message *msg = RECEIVE(MAKE_STR_TAG("ping"));
        SEND(msg->sender_pid, msg->payload, msg->size, MAKE_STR_TAG("pong"));
        MSG_FREE(msg);
    }
    EXIT_NORMAL();
}

TASK(pinger, Pid echo_pid, Pid report_to) {
    int ok = 1;
    for (int i = 0; i < ROUND_TRIPS; ++i) {
        SEND(echo_pid, &i, sizeof i, MAKE_STR_TAG("ping"));
        Message *reply = RECEIVE(MAKE_STR_TAG("pong"));
        ok &= *(int *)reply->payload == i;
        MSG_FREE(reply);
    }
    SEND(report_to, &ok, sizeof ok, MAKE_STR_TAG("done"));
    EXIT_NORMAL();
}

TASK(entry, int argc, char *argv[]) {
    (void)argc;
    (void)argv;

    for (int i = 0; i < PAIRS; ++i) {
        Pid echo_pid = SPAWN0(echo);
        TEST_ASSERT(echo_pid != INVALID_PID);
        TEST_ASSERT(SPAWN(pinger, 2, echo_pid, self->pid) != INVALID_PID);
    }

    for (int i = 0; i < PAIRS; ++i) {
        Message *done = RECEIVE(MAKE_STR_TAG("done"));
        TEST_ASSERT(*(int *)done->payload == 1);
        MSG_FREE(done);
    }

    TEST_PASS();
    EXIT_NORMAL();
}

CLPF_TEST_MAIN(entry)
