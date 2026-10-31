#include "test_helpers.h"
#include <api/task.h>
#include <api/message.h>

// Many short-lived processes that yield, wait and end, moved between
// schedulers by work stealing: their call stacks must follow them
#ifdef CLPF_SANITIZED
#define ROUNDS 20
#else
#define ROUNDS 100
#endif
#define WORKERS 64
#define HOPS    50

__attribute__((noinline)) static long nested(Process *self, int depth) {
    if (depth == 0) {
        YIELD();
        return 1;
    }
    return 1 + nested(self, depth - 1);
}

TASK(worker, Pid boss) {
    long total = 0;
    for (int i = 0; i < HOPS; ++i)
        total += nested(self, i % 8);
    SEND(boss, &total, sizeof total, MAKE_ATOM_TAG("done"));
}

TASK(entry, int argc, char *argv[]) {
    (void)argc;
    (void)argv;

    for (int round = 0; round < ROUNDS; ++round) {
        for (int i = 0; i < WORKERS; ++i)
            SPAWN(worker, 1, self->pid);
        for (int i = 0; i < WORKERS; ++i)
            MSG_FREE(RECEIVE(MAKE_ATOM_TAG("done")));
    }
    TEST_PASS();
}

CLPF_TEST_MAIN(entry)
