#include "test_helpers.h"
#include <api/task.h>
#include <api/message.h>
#include <api/link.h>
#include <api/monitor.h>
#include <api/supervisor.h>
#include <api/timer.h>

#define ROUNDS 200

// Never calls TRAP_EXIT: it relies on the spawn option
TASK0(guard) {
    Message *exit = RECEIVE_IF(TM_KIND, MAKE_TAG_KIND(TAG_KIND_EXIT));
    Pid boss = exit->sender_pid;
    MSG_FREE(exit);
    SEND(boss, NULL, 0, MAKE_ATOM_TAG("survived"));
}

TASK(entry, int argc, char *argv[]) {
    (void)argc;
    (void)argv;

    // An exit signal sent before the process first runs arrives as a message
    for (int i = 0; i < ROUNDS; ++i) {
        Pid pid = SPAWN_OPTS(guard, .trap_exit = true);
        SEND_EXIT(pid, EXIT_REASON_SHUTDOWN);
        Message *survived = RECEIVE_TIMEOUT(MAKE_ATOM_TAG("survived"), 1000);
        TEST_ASSERT(survived && survived->sender_pid == pid);
        MSG_FREE(survived);
    }

    // Supervisor children get it too
    Pid sup = SPAWN_SUPERVISOR("guards", SUP_ONE_FOR_ONE, 0, 0);
    Pid child = SUP_ADD_CHILD_OPTS(sup, RES_TEMPORARY, guard, .name = "guard", .trap_exit = true);
    TEST_ASSERT(child != INVALID_PID);
    SEND_EXIT(child, EXIT_REASON_SHUTDOWN);
    Message *survived = RECEIVE_TIMEOUT(MAKE_ATOM_TAG("survived"), 1000);
    TEST_ASSERT(survived && survived->sender_pid == child);
    MSG_FREE(survived);

    TEST_PASS();
}

CLPF_TEST_MAIN(entry)
