#include "test_helpers.h"
#include <api/task.h>
#include <api/message.h>
#include <api/introspection.h>
#include <api/timer.h>
#include <string.h>

// Overflows its default 16 KiB stack: the runtime names the process before
// crashing (ctest checks the message, not the exit code). With "recycled",
// the stack comes from a process that already ended.
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

TASK0(short_lived) {
    (void)self;
}

TASK(entry, int argc, char *argv[]) {
    if (argc > 1 && strcmp(argv[1], "recycled") == 0) {
        Pid pid = SPAWN0(short_lived);
        ProcessInfo info;
        while (PROCESS_INFO(pid, &info) == 0)
            SLEEP(1);
        SLEEP(10); // the main thread releases the stack right after the slot
    }

    SPAWN0_NAMED(runaway, "runaway");
    MSG_FREE(RECEIVE(MAKE_STR_TAG("never")));
    EXIT_NORMAL();
}

CLPF_TEST_MAIN(entry)
