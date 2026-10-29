#include "test_helpers.h"
#include <api/task.h>
#include <api/message.h>

#define DEPTH 2000 // about 300 KiB of stack, far above the 16 KiB default

static int recurse(int depth) {
    volatile char frame[128];
    frame[0] = (char)depth;
    return depth == 0 ? frame[0] : recurse(depth - 1) + frame[0];
}

TASK(deep, Pid parent) {
    int result = recurse(DEPTH);
    SEND(parent, &result, sizeof result, MAKE_STR_TAG("done"));
    EXIT_NORMAL();
}

// A process can be given a larger stack than the default
TASK(entry, int argc, char *argv[]) {
    (void)argc;
    (void)argv;

    Funcall_t call;
    funcall_init(&call, deep, 1, self->pid);
    Pid pid = clpf_spawn(self, &call, (SpawnOptions){ .name = "deep", .stack_size = 1024 * 1024 });
    TEST_ASSERT(pid != INVALID_PID);
    MSG_FREE(RECEIVE(MAKE_STR_TAG("done")));

    TEST_PASS();
    EXIT_NORMAL();
}

CLPF_TEST_MAIN(entry)
