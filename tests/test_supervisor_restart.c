#include "test_helpers.h"
#include <api/supervisor.h>
#include <api/monitor.h>
#include <api/timer.h>

#define PERIOD_MS 10000
#define CHILDREN 30
#define ALWAYS_CRASHING_LIMIT 3

static atomic_bool crashed[CHILDREN];

// Crashes right after its first start, possibly before the supervisor got
// the reply to its spawn request: every crash must still be seen
TASK(crash_first_time, Pid report_to, int index) {
    SEND(report_to, NULL, 0, MAKE_STR_TAG("started"));
    if (!atomic_exchange(&crashed[index], true))
        EXIT_ERROR();
    MSG_FREE(RECEIVE(MAKE_STR_TAG("never")));
}

TASK(always_crash, Pid report_to) {
    SEND(report_to, NULL, 0, MAKE_STR_TAG("crashing"));
    EXIT_ERROR();
}

TASK0(idle) {
    MSG_FREE(RECEIVE(MAKE_STR_TAG("never")));
}

static unsigned int down_reason(Process *self, Pid pid) {
    Message *msg = RECEIVE(MAKE_DOWN_TAG(pid));
    unsigned int reason = ((DownInfo *)msg->payload)->reason;
    MSG_FREE(msg);
    return reason;
}

TASK(entry, int argc, char *argv[]) {
    (void)argc;
    (void)argv;

    // Every crash is seen and restarted, within the supervisor's intensity
    Pid sup = SPAWN_SUPERVISOR("sup", SUP_ONE_FOR_ONE, PERIOD_MS, CHILDREN + 1);
    MONITOR(sup);
    for (int i = 0; i < CHILDREN; ++i)
        TEST_ASSERT(SUP_ADD_CHILD(sup, RES_PERMANENT, crash_first_time, "crasher", 2, self->pid, i) != INVALID_PID);
    for (int i = 0; i < 2 * CHILDREN; ++i)
        MSG_FREE(RECEIVE(MAKE_STR_TAG("started")));
    SLEEP(50);
    TEST_ASSERT(FETCH(MAKE_STR_TAG("started")) == NULL);
    TEST_ASSERT(FETCH(MAKE_DOWN_TAG(sup)) == NULL);

    // Past its intensity, a supervisor stops its children and fails visibly
    Pid fragile = SPAWN_SUPERVISOR("fragile", SUP_ONE_FOR_ONE, PERIOD_MS, ALWAYS_CRASHING_LIMIT);
    MONITOR(fragile);
    Pid bystander = SUP_ADD_CHILD0(fragile, RES_PERMANENT, idle, "bystander");
    MONITOR(bystander);
    SUP_ADD_CHILD(fragile, RES_PERMANENT, always_crash, "always_crash", 1, self->pid);
    // The first start, then ALWAYS_CRASHING_LIMIT restarts; the next crash is one too many
    for (int i = 0; i < 1 + ALWAYS_CRASHING_LIMIT; ++i)
        MSG_FREE(RECEIVE(MAKE_STR_TAG("crashing")));
    TEST_ASSERT(down_reason(self, bystander) == EXIT_REASON_SHUTDOWN);
    TEST_ASSERT(down_reason(self, fragile) == EXIT_REASON_ERROR);
    TEST_ASSERT(FETCH(MAKE_STR_TAG("crashing")) == NULL);

    TEST_PASS();
    EXIT_NORMAL();
}

CLPF_TEST_MAIN(entry)
