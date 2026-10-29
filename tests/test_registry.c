#include "test_helpers.h"
#include <api/task.h>
#include <api/message.h>
#include <api/monitor.h>
#include <api/registry.h>

TASK0(wait_then_exit) {
    MSG_FREE(RECEIVE(MAKE_STR_TAG("go")));
    EXIT_NORMAL();
}

TASK(entry, int argc, char *argv[]) {
    (void)argc;
    (void)argv;

    // A spawn name is only a label: it is not registered, so labels may repeat
    Pid first = SPAWN0_NAMED(wait_then_exit, "worker");
    Pid second = SPAWN0_NAMED(wait_then_exit, "worker");
    TEST_ASSERT(WHEREIS("worker") == INVALID_PID);

    // REGISTER answers right away
    TEST_ASSERT(REGISTER("db", first) == 0);
    TEST_ASSERT(WHEREIS("db") == first);
    TEST_ASSERT(REGISTER("db", second) == -1);       // name taken
    TEST_ASSERT(REGISTER("cache", first) == -1);     // one name per process
    TEST_ASSERT(REGISTER("bad name", second) == -1); // invalid character
    TEST_ASSERT(REGISTER("", second) == -1);

    TEST_ASSERT(UNREGISTER("db") == 0);
    TEST_ASSERT(UNREGISTER("db") == -1);
    TEST_ASSERT(WHEREIS("db") == INVALID_PID);
    TEST_ASSERT(REGISTER("cache", first) == 0);

    // A name is released when its process exits
    TEST_ASSERT(REGISTER("db", second) == 0);
    MONITOR(second);
    SEND(second, NULL, 0, MAKE_STR_TAG("go"));
    MSG_FREE(RECEIVE(MAKE_DOWN_TAG(second)));
    TEST_ASSERT(REGISTER("db", self->pid) == 0); // the name of a dead process is free
    TEST_ASSERT(WHEREIS("db") == self->pid);

    // A dead process cannot be registered
    TEST_ASSERT(REGISTER("ghost", second) == -1);

    SEND(first, NULL, 0, MAKE_STR_TAG("go"));
    TEST_PASS();
    EXIT_NORMAL();
}

CLPF_TEST_MAIN(entry)
