#include "test_helpers.h"
#include <api/task.h>
#include <api/message.h>

// Overflows its default 16 KiB stack: the runtime names the process before
// crashing (ctest checks the message, not the exit code)
// NOLINTNEXTLINE(clang-diagnostic-infinite-recursion): overflowing is the point
static int recurse(int depth) {
    volatile char frame[128];
    frame[0] = (char)depth;
    return recurse(depth + 1) + frame[0];
}

TASK0(runaway) {
    (void)self;
    recurse(0);
}

TASK(entry, int argc, char *argv[]) {
    (void)argc;
    (void)argv;

    SPAWN0_NAMED(runaway, "runaway");
    MSG_FREE(RECEIVE(MAKE_STR_TAG("never")));
    EXIT_NORMAL();
}

CLPF_TEST_MAIN(entry)
