#include "test_helpers.h"
#include <api/task.h>
#include <api/message.h>
#include <api/timer.h>
#include <api/link.h>

// Answers every request after `delay_ms`, with the request's value plus one
TASK(server, int delay_ms) {
    while (1) {
        Message *msg = RECEIVE(NO_TAG);
        SLEEP((uint64_t)delay_ms);
        int value = msg->payload ? *(int *)msg->payload + 1 : 0;
        REPLY(msg, &value, sizeof value);
        MSG_FREE(msg);
    }
}

TASK0(silent) {
    while (1)
        MSG_FREE(RECEIVE(NO_TAG));
}

TASK(entry, int argc, char *argv[]) {
    (void)argc;
    (void)argv;

    Ref first = MAKE_REF(), second = MAKE_REF();
    TEST_ASSERT(first != 0 && second != first);

    // A request with a unique reference gets its own reply
    Pid fast = SPAWN(server, 1, 0);
    int value = 41;
    Message *reply = REQUEST_REF(fast, &value, sizeof value, 1000);
    TEST_ASSERT(reply && *(int *)reply->payload == 42);
    MSG_FREE(reply);

    // A reply arriving after the timeout is dropped, never left in the mailbox
    Pid slow = SPAWN(server, 1, 50);
    TEST_ASSERT(REQUEST_REF(slow, &value, sizeof value, 10) == NULL);
    SLEEP(100);
    TEST_ASSERT(FETCH(NO_TAG) == NULL);

    // No process: no wait
    KILL(slow);
    SLEEP(10);
    TEST_ASSERT(REQUEST_REF(slow, &value, sizeof value, CLPF_INFINITY) == NULL);

    // REQUEST_TIMEOUT bounds a plain request, and REPLY answers it with its tag
    Pid quiet = SPAWN0(silent);
    TEST_ASSERT(REQUEST_TIMEOUT(quiet, NULL, 0, MAKE_STR_TAG("ping"), 20) == NULL);
    reply = REQUEST_TIMEOUT(fast, &value, sizeof value, MAKE_STR_TAG("add"), CLPF_INFINITY);
    TEST_ASSERT(reply && MSG_IS(reply, MAKE_STR_TAG("add")) && *(int *)reply->payload == 42);
    MSG_FREE(reply);

    TEST_PASS();
}

CLPF_TEST_MAIN(entry)
