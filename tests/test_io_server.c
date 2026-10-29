#include "test_helpers.h"
#include <api/task.h>
#include <api/io.h>
#include <api/registry.h>
#include <api/monitor.h>
#include <api/link.h>
#include <api/timer.h>

#define MAX_WAIT_MS 2000

// The I/O server is supervised: killed, it comes back under the same name
TASK(entry, int argc, char *argv[]) {
    (void)argc;
    (void)argv;

    Pid io = WHEREIS("server::io");
    TEST_ASSERT(io != INVALID_PID);
    TEST_ASSERT(PRINT("before\n") == 0);

    MONITOR(io);
    TEST_ASSERT(KILL(io) == 0);
    MSG_FREE(RECEIVE(MAKE_DOWN_TAG(io)));

    Pid restarted = INVALID_PID;
    for (int waited = 0; waited < MAX_WAIT_MS; waited += 10) {
        restarted = WHEREIS("server::io");
        if (restarted != INVALID_PID && restarted != io)
            break;
        SLEEP(10);
    }
    TEST_ASSERT(restarted != INVALID_PID && restarted != io);
    TEST_ASSERT(PRINT("after\n") == 0);

    TEST_PASS();
    EXIT_NORMAL();
}

CLPF_TEST_MAIN(entry)
