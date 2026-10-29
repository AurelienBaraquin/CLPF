#include "test_helpers.h"
#include <api/task.h>
#include <api/message.h>
#include <api/memory.h>

TASK(echo, Pid parent) {
    int *n = RECEIVE(MAKE_STR_TAG("ping"));
    int reply = *n + 1;
    FREE(n);
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
    int *r = RECEIVE(MAKE_STR_TAG("pong"));
    TEST_ASSERT(*r == 42);
    FREE(r);

    TEST_PASS();
    EXIT_NORMAL();
}

CLPF_TEST_MAIN(entry)
