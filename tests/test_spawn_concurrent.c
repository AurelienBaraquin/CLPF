#include "test_helpers.h"
#include <api/task.h>
#include <api/message.h>
#include <api/memory.h>
#include <stdlib.h>

#define SPAWNERS 4
#ifdef CLPF_SANITIZED
#define CHILDREN_PER_SPAWNER 100
#else
#define CHILDREN_PER_SPAWNER 500
#endif
#define TOTAL_CHILDREN (SPAWNERS * CHILDREN_PER_SPAWNER)

typedef struct {
    Pid pid;
    Pid scheduler;
} Hello;

TASK(child, Pid report_to) {
    Hello hello = { self->pid, self->scheduler_pid };
    SEND(report_to, &hello, sizeof hello, MAKE_STR_TAG("hello"));
    EXIT_NORMAL();
}

// Several processes spawn at the same time from different schedulers
TASK(spawner, Pid report_to) {
    for (int i = 0; i < CHILDREN_PER_SPAWNER; ++i) {
        if (SPAWN(child, 1, report_to) == INVALID_PID)
            SEND(report_to, NULL, 0, MAKE_STR_TAG("spawn_failed"));
    }
    EXIT_NORMAL();
}

static int compare_pids(const void *a, const void *b) {
    Pid x = *(const Pid *)a, y = *(const Pid *)b;
    return (x > y) - (x < y);
}

TASK(entry, int argc, char *argv[]) {
    (void)argc;
    (void)argv;

    for (int i = 0; i < SPAWNERS; ++i)
        TEST_ASSERT(SPAWN(spawner, 1, self->pid) != INVALID_PID);

    static Pid pids[TOTAL_CHILDREN];
    bool used_scheduler[MAX_SCHEDULERS] = {0};
    for (int i = 0; i < TOTAL_CHILDREN; ++i) {
        Hello *hello = RECEIVE(MAKE_STR_TAG("hello"));
        pids[i] = hello->pid;
        used_scheduler[hello->scheduler] = true;
        FREE(hello);
    }
    TEST_ASSERT(FETCH(MAKE_STR_TAG("spawn_failed")) == NULL);

    // Every spawn got its own pid
    qsort(pids, TOTAL_CHILDREN, sizeof(Pid), compare_pids);
    for (int i = 1; i < TOTAL_CHILDREN; ++i)
        TEST_ASSERT(pids[i] != pids[i - 1]);

    // New processes go to the least loaded schedulers, not all to their parent's
    int schedulers_used = 0;
    for (int i = 0; i < MAX_SCHEDULERS; ++i)
        schedulers_used += used_scheduler[i];
    TEST_ASSERT(schedulers_used == clpf.scheduler_count);

    TEST_PASS();
    EXIT_NORMAL();
}

CLPF_TEST_MAIN(entry)
