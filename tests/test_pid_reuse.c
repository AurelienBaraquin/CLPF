#include "test_helpers.h"
#include <api/task.h>
#include <api/message.h>
#include <api/memory.h>

#define MAX_WAIT_YIELDS 100000

TASK(short_lived, Pid parent) {
    SEND(parent, NULL, 0, MAKE_STR_TAG("bye"));
    EXIT_NORMAL();
}

// Once a process is gone, its pid must never designate another process,
// and sending to it must fail cleanly instead of touching freed memory.
TASK(entry, int argc, char *argv[]) {
    (void)argc;
    (void)argv;

    Pid first = SPAWN(short_lived, 1, self->pid);
    TEST_ASSERT(first != INVALID_PID);
    RECEIVE(MAKE_STR_TAG("bye"));

    int yields = 0;
    while (SEND(first, NULL, 0, MAKE_STR_TAG("ping")) == 0 && yields < MAX_WAIT_YIELDS)
        ++yields;
    TEST_ASSERT(yields < MAX_WAIT_YIELDS);

    Pid second = SPAWN(short_lived, 1, self->pid);
    TEST_ASSERT(second != INVALID_PID);
    TEST_ASSERT(second != first);
    TEST_ASSERT(SEND(first, NULL, 0, MAKE_STR_TAG("ping")) == -1);
    RECEIVE(MAKE_STR_TAG("bye"));

    TEST_ASSERT(SEND(INVALID_PID, NULL, 0, MAKE_STR_TAG("ping")) == -1);
    TEST_ASSERT(SEND(MAX_PROCESSES * 3 + 7, NULL, 0, MAKE_STR_TAG("ping")) == -1);

    TEST_PASS();
    EXIT_NORMAL();
}

CLPF_TEST_MAIN(entry)
