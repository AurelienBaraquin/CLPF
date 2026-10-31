#include "test_helpers.h"
#include <api/task.h>
#include <api/message.h>
#include <api/introspection.h>
#include <utils/time.h>

#define CHILDREN 8
#define WORK_MS  200

TASK0(idle) {
    MSG_FREE(RECEIVE(MAKE_ATOM_TAG("never")));
}

// CPU-bound: records every scheduler it ran on
TASK(hog, Pid boss) {
    unsigned int seen = 0;
    uint64_t end = now_ms() + WORK_MS;
    while (now_ms() < end) {
        seen |= 1u << self->scheduler_pid;
        REDUCE(10);
    }
    SEND(boss, &seen, sizeof seen, MAKE_ATOM_TAG("seen"));
}

TASK(entry, int argc, char *argv[]) {
    (void)argc;
    (void)argv;

    // A child starts on its parent's scheduler: processes that talk stay together
    for (int i = 0; i < CHILDREN; ++i) {
        Pid pid = SPAWN0(idle);
        ProcessInfo info;
        TEST_ASSERT(PROCESS_INFO(pid, &info) == 0 && info.scheduler == (int)self->scheduler_pid);
    }

    // Busy children still end up spread by work stealing
    for (int i = 0; i < CHILDREN; ++i)
        SPAWN(hog, 1, self->pid);
    unsigned int used = 0;
    for (int i = 0; i < CHILDREN; ++i) {
        Message *msg = RECEIVE(MAKE_ATOM_TAG("seen"));
        used |= *(unsigned int *)msg->payload;
        MSG_FREE(msg);
    }
    TEST_ASSERT(clpf.scheduler_count == 1 || __builtin_popcount(used) > 1);

    TEST_PASS();
}

CLPF_TEST_MAIN(entry)
