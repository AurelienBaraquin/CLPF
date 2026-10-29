#include "test_helpers.h"
#include <api/task.h>
#include <api/message.h>
#include <utils/time.h>

#define HOGS 8
#define WORK_MS 200

// CPU-bound: records every scheduler it ran on
TASK(hog, Pid report_to) {
    unsigned int seen = 0;
    uint64_t end = now_ms() + WORK_MS;
    while (now_ms() < end) {
        seen |= 1u << self->scheduler_pid;
        REDUCE(10);
    }
    SEND(report_to, &seen, sizeof seen, MAKE_STR_TAG("seen"));
    EXIT_NORMAL();
}

// All hogs start on scheduler 0: idle schedulers must take some of them
TASK(entry, int argc, char *argv[]) {
    (void)argc;
    (void)argv;

    for (int i = 0; i < HOGS; ++i) {
        Funcall_t call;
        funcall_init(&call, hog, 1, self->pid);
        TEST_ASSERT(clpf_spawn(self, &call, (SpawnOptions){ .scheduler = 1 }) != INVALID_PID);
    }

    unsigned int used = 0;
    for (int i = 0; i < HOGS; ++i) {
        Message *msg = RECEIVE(MAKE_STR_TAG("seen"));
        used |= *(unsigned int *)msg->payload;
        MSG_FREE(msg);
    }

    int schedulers_used = __builtin_popcount(used);
    fprintf(stderr, "hogs ran on %d of %d schedulers\n", schedulers_used, clpf.scheduler_count);
    TEST_ASSERT(schedulers_used == (clpf.scheduler_count < HOGS ? clpf.scheduler_count : HOGS));

    TEST_PASS();
    EXIT_NORMAL();
}

CLPF_TEST_MAIN(entry)
