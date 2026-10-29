#include "test_helpers.h"
#include <api/supervisor.h>
#include <api/memory.h>

#define PERIOD_MS 10000
#define MAX_RESTARTS 3
#define CHILDREN 30
#define SETTLE_YIELDS 20000

// The child crashes right after starting, possibly before the supervisor
// got the reply to its spawn request: every crash must still be seen.
TASK(crasher, Pid report_to) {
    SEND(report_to, NULL, 0, MAKE_STR_TAG("started"));
    EXIT_ERROR();
}

TASK(entry, int argc, char *argv[]) {
    (void)argc;
    (void)argv;

    Pid sup = SPAWN_SUPERVISOR("sup", SUP_ONE_FOR_ONE, PERIOD_MS, MAX_RESTARTS);
    TEST_ASSERT(sup != INVALID_PID);
    for (int i = 0; i < CHILDREN; ++i)
        TEST_ASSERT(SUP_ADD_CHILD(sup, RES_PERMANENT, crasher, "crasher", 1, self->pid) != INVALID_PID);

    // Each child: the first start plus MAX_RESTARTS restarts, then the supervisor gives up on it
    for (int i = 0; i < CHILDREN * (1 + MAX_RESTARTS); ++i)
        MSG_FREE(RECEIVE(MAKE_STR_TAG("started")));

    for (int i = 0; i < SETTLE_YIELDS; ++i)
        YIELD();
    TEST_ASSERT(FETCH(MAKE_STR_TAG("started")) == NULL);

    TEST_PASS();
    EXIT_NORMAL();
}

CLPF_TEST_MAIN(entry)
