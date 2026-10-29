#include "test_helpers.h"
#include <api/task.h>
#include <api/message.h>
#include <api/memory.h>
#include <utils/time.h>

#define ROUND_TRIPS     5000
#define MAX_DURATION_MS 2000

// A context switch must not cost a scheduler sleep: thousands of
// round trips between two processes should complete well under a second.
TASK(echo, Pid parent) {
    for (int i = 0; i < ROUND_TRIPS; ++i) {
        MSG_FREE(RECEIVE(MAKE_STR_TAG("ping")));
        SEND(parent, &i, sizeof i, MAKE_STR_TAG("pong"));
    }
    EXIT_NORMAL();
}

TASK(entry, int argc, char *argv[]) {
    (void)argc;
    (void)argv;

    Pid child = SPAWN(echo, 1, self->pid);
    TEST_ASSERT(child != INVALID_PID);

    uint64_t start = now_ms();
    for (int i = 0; i < ROUND_TRIPS; ++i) {
        SEND(child, &i, sizeof i, MAKE_STR_TAG("ping"));
        Message *reply = RECEIVE(MAKE_STR_TAG("pong"));
        TEST_ASSERT(*(int *)reply->payload == i);
        MSG_FREE(reply);
    }
    uint64_t elapsed = now_ms() - start;
    fprintf(stderr, "%d round trips in %lu ms\n", ROUND_TRIPS, (unsigned long)elapsed);
    TEST_ASSERT(elapsed < MAX_DURATION_MS);

    TEST_PASS();
    EXIT_NORMAL();
}

CLPF_TEST_MAIN(entry)
