#include "test_helpers.h"
#include <api/task.h>
#include <api/message.h>
#include <utils/time.h>

#define RUN_MS 200

// Counts how many times it gets the CPU in RUN_MS
TASK(spinner, Pid report_to) {
    long turns = 0;
    uint64_t end = now_ms() + RUN_MS;
    while (now_ms() < end) {
        ++turns;
        YIELD();
    }
    SEND(report_to, &turns, sizeof turns, MAKE_STR_TAG("turns"));
    EXIT_NORMAL();
}

// On a single scheduler, a low priority process runs about once for every 8
// turns of a medium priority one (ctest pins it to one CPU)
TASK(entry, int argc, char *argv[]) {
    (void)argc;
    (void)argv;

    Pid medium = SPAWN(spinner, 1, self->pid);
    Pid low = SPAWN_PRIO(spinner, PROCESS_PRIORITY_LOW, 1, self->pid);

    long medium_turns = 0, low_turns = 0;
    for (int i = 0; i < 2; ++i) {
        Message *msg = RECEIVE(MAKE_STR_TAG("turns"));
        long turns = *(long *)msg->payload;
        if (msg->sender_pid == medium)
            medium_turns = turns;
        else if (msg->sender_pid == low)
            low_turns = turns;
        MSG_FREE(msg);
    }

    double ratio = (double)low_turns / (double)medium_turns;
    fprintf(stderr, "medium %ld turns, low %ld turns, ratio %.3f\n", medium_turns, low_turns, ratio);
    TEST_ASSERT(ratio > 1.0 / 16 && ratio < 1.0 / 4);

    TEST_PASS();
    EXIT_NORMAL();
}

CLPF_TEST_MAIN(entry)
