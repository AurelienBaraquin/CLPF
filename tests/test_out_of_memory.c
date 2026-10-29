#include "test_helpers.h"
#include <api/task.h>
#include <api/message.h>
#include <api/memory.h>

#define HUGE ((size_t)1 << 44) // 16 TiB: no system can map it

TASK0(receiver) {
    MSG_FREE(RECEIVE(MAKE_STR_TAG("never")));
}

// Running out of memory is reported, not fatal
TASK(entry, int argc, char *argv[]) {
    (void)argc;
    (void)argv;

    TEST_ASSERT(ALLOC(HUGE) == NULL);

    // The receiver's heap cannot hold the message: SEND fails, nothing is copied
    char small = 0;
    Pid pid = SPAWN0(receiver);
    TEST_ASSERT(SEND(pid, &small, HUGE, MAKE_STR_TAG("huge")) == -1);

    // The process and the runtime carry on
    void *fine = ALLOC(64);
    TEST_ASSERT(fine != NULL);
    FREE(fine);
    TEST_ASSERT(SEND(pid, &small, sizeof small, MAKE_STR_TAG("small")) == 0);

    TEST_PASS();
    EXIT_NORMAL();
}

CLPF_TEST_MAIN(entry)
