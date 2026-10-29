#include "test_helpers.h"
#include <api/task.h>
#include <api/io.h>
#include <api/link.h>

#define LINES 1000

TASK0(forever) {
    while (1)
        YIELD();
}

TASK0(stubborn) {
    TRAP_EXIT(true);
    MSG_FREE(RECEIVE(MAKE_STR_TAG("never")));
}

// The entry task ends with an error while other processes would run forever:
// the runtime must still stop, write every line, and exit with 1 (checked by ctest)
TASK(entry, int argc, char *argv[]) {
    (void)argc;
    (void)argv;

    SPAWN0(forever);
    SPAWN0(stubborn);
    for (int i = 0; i < LINES; ++i)
        PRINT("line %d\n", i);
    EXIT_ERROR();
}

CLPF_TEST_MAIN(entry)
