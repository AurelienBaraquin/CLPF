#include "test_helpers.h"
#include <api/task.h>
#include <api/message.h>
#include <api/timer.h>

// Never yields explicitly: only its reduction budget lets the others run.
// Run on a single scheduler, so without preemption nothing else would ever run.
TASK0(hog) {
    while (FETCH(MAKE_STR_TAG("stop")) == NULL)
        REDUCE(1);
    EXIT_NORMAL();
}

// Sends without ever blocking: sending costs reductions too
TASK(spammer, Pid target) {
    for (int i = 0; i < 100000; ++i)
        SEND(target, NULL, 0, MAKE_STR_TAG("spam"));
    EXIT_NORMAL();
}

TASK0(sink) {
    while (1)
        MSG_FREE(RECEIVE(MAKE_STR_TAG("spam")));
}

TASK(entry, int argc, char *argv[]) {
    (void)argc;
    (void)argv;

    Pid hog_pid = SPAWN0(hog);
    Pid sink_pid = SPAWN0(sink);
    SPAWN(spammer, 1, sink_pid);

    // Timers and this process keep running next to the hog
    SLEEP(20);
    SEND(hog_pid, NULL, 0, MAKE_STR_TAG("stop"));

    TEST_PASS();
    EXIT_NORMAL();
}

CLPF_TEST_MAIN(entry)
