#include "test_helpers.h"
#include <api/task.h>
#include <api/message.h>
#include <api/memory.h>

TASK(echo, Pid parent) {
    Message *msg = RECEIVE(MAKE_STR_TAG("ping"));
    int reply = *(int *)msg->payload + 1;
    MSG_FREE(msg);
    SEND(parent, &reply, sizeof reply, MAKE_STR_TAG("pong"));
    EXIT_NORMAL();
}

TASK(entry, int argc, char *argv[]) {
    (void)argc;
    (void)argv;

    Pid child = SPAWN(echo, 1, self->pid);
    TEST_ASSERT(child != INVALID_PID);

    int n = 41;
    SEND(child, &n, sizeof n, MAKE_STR_TAG("ping"));
    Message *reply = RECEIVE(MAKE_STR_TAG("pong"));
    TEST_ASSERT(*(int *)reply->payload == 42);
    MSG_FREE(reply);

    TEST_PASS();
    EXIT_NORMAL();
}

CLPF_TEST_MAIN(entry)
