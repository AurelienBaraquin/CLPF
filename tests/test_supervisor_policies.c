#include "test_helpers.h"
#include <api/supervisor.h>
#include <api/memory.h>

#define PERIOD_MS 10000
#define MAX_RESTARTS 3
#define SETTLE_YIELDS 20000

TASK(exit_normally, Pid report_to) {
    SEND(report_to, NULL, 0, MAKE_STR_TAG("normal"));
    EXIT_NORMAL();
}

TASK(crash_once, Pid report_to) {
    SEND(report_to, NULL, 0, MAKE_STR_TAG("crash"));
    EXIT_ERROR();
}

TASK(crash_temporary, Pid report_to) {
    SEND(report_to, NULL, 0, MAKE_STR_TAG("temporary"));
    EXIT_ERROR();
}

TASK0(idle) {
    RECEIVE(MAKE_STR_TAG("never"));
}

static void settle(Process *self) {
    for (int i = 0; i < SETTLE_YIELDS; ++i)
        YIELD();
}

TASK(entry, int argc, char *argv[]) {
    (void)argc;
    (void)argv;

    Pid sup = SPAWN_SUPERVISOR("sup", SUP_ONE_FOR_ONE, PERIOD_MS, MAX_RESTARTS);
    TEST_ASSERT(sup != INVALID_PID);

    // A transient child that exits normally is not restarted
    TEST_ASSERT(SUP_ADD_CHILD(sup, RES_TRANSIENT, exit_normally, "transient_ok", 1, self->pid) != INVALID_PID);
    RECEIVE(MAKE_STR_TAG("normal"));

    // A transient child that crashes is restarted
    TEST_ASSERT(SUP_ADD_CHILD(sup, RES_TRANSIENT, crash_once, "transient_crash", 1, self->pid) != INVALID_PID);
    for (int i = 0; i < 1 + MAX_RESTARTS; ++i)
        RECEIVE(MAKE_STR_TAG("crash"));

    // A temporary child is never restarted
    TEST_ASSERT(SUP_ADD_CHILD(sup, RES_TEMPORARY, crash_temporary, "temporary", 1, self->pid) != INVALID_PID);
    RECEIVE(MAKE_STR_TAG("temporary"));

    settle(self);
    TEST_ASSERT(FETCH(MAKE_STR_TAG("normal")) == NULL);
    TEST_ASSERT(FETCH(MAKE_STR_TAG("crash")) == NULL);
    TEST_ASSERT(FETCH(MAKE_STR_TAG("temporary")) == NULL);

    // Removing a child answers instead of blocking the caller
    Pid idle_pid = SUP_ADD_CHILD0(sup, RES_PERMANENT, idle, "idle");
    TEST_ASSERT(idle_pid != INVALID_PID);
    TEST_ASSERT(SUP_REMOVE_CHILD(sup, idle_pid) == 0);
    TEST_ASSERT(SUP_REMOVE_CHILD(sup, idle_pid) == -1);

    // Strategies that are not implemented are refused instead of duplicating children
    Pid all = SPAWN_SUPERVISOR("all", SUP_ONE_FOR_ALL, PERIOD_MS, MAX_RESTARTS);
    settle(self);
    TEST_ASSERT(SUP_ADD_CHILD0(all, RES_PERMANENT, idle, "idle_all") == INVALID_PID);

    TEST_PASS();
    EXIT_NORMAL();
}

CLPF_TEST_MAIN(entry)
