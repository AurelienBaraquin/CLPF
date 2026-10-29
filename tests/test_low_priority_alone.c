#include "test_helpers.h"
#include <api/task.h>
#include <api/message.h>

#define ROUNDS 100

// A low priority process is only delayed relative to others: when it is the
// only ready process of its scheduler, the scheduler must run it, not sleep.
TASK(low, Pid parent) {
    for (int i = 0; i < ROUNDS; ++i)
        YIELD();
    SEND(parent, NULL, 0, MAKE_STR_TAG("done"));
    EXIT_NORMAL();
}

TASK(entry, int argc, char *argv[]) {
    (void)argc;
    (void)argv;

    TEST_ASSERT(SPAWN_PRIO(low, PROCESS_PRIORITY_LOW, 1, self->pid) != INVALID_PID);
    RECEIVE(MAKE_STR_TAG("done"));

    TEST_PASS();
    EXIT_NORMAL();
}

CLPF_TEST_MAIN(entry)
