#include "test_helpers.h"
#include <api/task.h>
#include <api/message.h>
#include <api/memory.h>
#include <utils/time.h>

// ThreadSanitizer follows each process as a fiber, at a cost that grows with
// the live ones: under sanitizers the bound only catches gross regressions
#ifdef CLPF_SANITIZED
#define WAITING_PROCESSES 1000
#define MAX_DURATION_MS   8000
#else
#define WAITING_PROCESSES 10000
#define MAX_DURATION_MS   1500
#endif
#define ROUND_TRIPS 5000

// Thousands of processes blocked in RECEIVE must not slow down the others:
// a scheduler only looks at the processes that actually received something.
TASK0(sleeper) {
    MSG_FREE(RECEIVE(MAKE_STR_TAG("never")));
}

TASK(echo, Pid parent) {
    for (int i = 0; i < ROUND_TRIPS; ++i) {
        MSG_FREE(RECEIVE(MAKE_STR_TAG("ping")));
        SEND(parent, &i, sizeof i, MAKE_STR_TAG("pong"));
    }
    EXIT_NORMAL();
}

TASK(entry, int argc, char *argv[]) {
    (void)argc;
    (void)argv;

    for (int i = 0; i < WAITING_PROCESSES; ++i)
        SPAWN0(sleeper);

    Pid child = SPAWN(echo, 1, self->pid);
    TEST_ASSERT(child != INVALID_PID);

    uint64_t start = now_ms();
    for (int i = 0; i < ROUND_TRIPS; ++i) {
        SEND(child, &i, sizeof i, MAKE_STR_TAG("ping"));
        MSG_FREE(RECEIVE(MAKE_STR_TAG("pong")));
    }
    uint64_t elapsed = now_ms() - start;
    fprintf(stderr, "%d round trips next to %d waiting processes in %lu ms\n", ROUND_TRIPS, WAITING_PROCESSES,
            (unsigned long)elapsed);
    TEST_ASSERT(elapsed < MAX_DURATION_MS);

    TEST_PASS();
    EXIT_NORMAL();
}

CLPF_TEST_MAIN(entry)
