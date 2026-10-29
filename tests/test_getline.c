#include "test_helpers.h"
#include <api/task.h>
#include <api/io.h>
#include <api/timer.h>
#include <string.h>

TASK(reader, Pid parent) {
    char *line = GETLINE();
    SEND(parent, line, line ? strlen(line) + 1 : 0, MAKE_STR_TAG("line"));
    FREE(line);
    EXIT_NORMAL();
}

// "read": reads two lines and the end of input from stdin.
// "wait": a GETLINE waiting for input must not hold back PRINT (ctest kills it after "early").
TASK(entry, int argc, char *argv[]) {
    if (argc > 1 && strcmp(argv[1], "read") == 0) {
        char *first = GETLINE();
        char *second = GETLINE();
        char *end = GETLINE();
        TEST_ASSERT(first && strcmp(first, "hello\n") == 0);
        TEST_ASSERT(second && strcmp(second, "world\n") == 0);
        TEST_ASSERT(end == NULL);
        FREE(first);
        FREE(second);
        TEST_PASS();
        EXIT_NORMAL();
    }

    SPAWN(reader, 1, self->pid);
    SLEEP(50);
    PRINT("early\n");
    MSG_FREE(RECEIVE(MAKE_STR_TAG("line")));
    TEST_PASS();
    EXIT_NORMAL();
}

CLPF_TEST_MAIN(entry)
