#include "test_helpers.h"
#include <api/task.h>
#include <api/message.h>
#include <api/timer.h>
#include <utils/time.h>
#include <string.h>

#ifdef CLPF_SANITIZED
#define CROWD 1000
#else
#define CROWD 10000
#endif
// Small enough for the stack and heap pools to serve every spawn of a batch
#define BATCH           500
#define ROUNDS          8
#define ORPHANS         8
#define MAX_WAIT_MS     5000
#define CROWD_SLOWDOWN  2
#define TIMING_SLACK_MS 50

static Pid crowd[CROWD];
static Pid batch[BATCH];

// Waits for "stop", then ends
TASK0(child) {
    MSG_FREE(RECEIVE(MAKE_STR_TAG("stop")));
}

// Hands the pids of its own children to boss, then ends before them
TASK(short_parent, Pid boss) {
    Pid orphans[ORPHANS];
    for (int i = 0; i < ORPHANS; ++i)
        orphans[i] = SPAWN0(child);
    SEND(boss, orphans, sizeof orphans, MAKE_STR_TAG("orphans"));
}

static bool wait_children(Process *self, size_t count) {
    uint64_t deadline = now_ms() + MAX_WAIT_MS;
    while (NB_CHILDREN() != count && now_ms() < deadline)
        SLEEP(1);
    return NB_CHILDREN() == count;
}

static bool is_child(Process *self, Pid pid) {
    for (size_t i = 0; i < NB_CHILDREN(); ++i) {
        if (CHILD(i) == pid)
            return true;
    }
    return false;
}

static void spawn_all(Process *self, Pid *pids, int count) {
    for (int i = 0; i < count; ++i) {
        pids[i] = SPAWN0(child);
        TEST_ASSERT(pids[i] != INVALID_PID);
        REDUCE(50); // SPAWN never yields: let the children reach RECEIVE
    }
}

// Spawns BATCH children next to `others` and loses them, ROUNDS times: the time it takes
static uint64_t spawn_and_lose_batches(Process *self, size_t others) {
    uint64_t start = now_ms();
    for (int round = 0; round < ROUNDS; ++round) {
        spawn_all(self, batch, BATCH);
        TEST_ASSERT(NB_CHILDREN() == others + BATCH);
        for (int i = 0; i < BATCH; ++i)
            SEND(batch[i], NULL, 0, MAKE_STR_TAG("stop"));
        TEST_ASSERT(wait_children(self, others));
    }
    return now_ms() - start;
}

// A parent with many live children spawns and loses one in constant time
TASK(entry, int argc, char *argv[]) {
    (void)argc;
    (void)argv;

    spawn_all(self, crowd, CROWD);
    TEST_ASSERT(NB_CHILDREN() == CROWD);
    TEST_ASSERT(CHILD(0) == crowd[CROWD - 1]);
    TEST_ASSERT(CHILD(CROWD - 1) == crowd[0]);
    TEST_ASSERT(CHILD(CROWD) == INVALID_PID);

    spawn_and_lose_batches(self, CROWD); // refills the pools the crowd emptied
    uint64_t crowded = spawn_and_lose_batches(self, CROWD);

    // Every other child of the crowd ends, oldest first: the others keep their order
    for (int i = 0; i < CROWD; i += 2)
        SEND(crowd[i], NULL, 0, MAKE_STR_TAG("stop"));
    TEST_ASSERT(wait_children(self, CROWD / 2));
    TEST_ASSERT(CHILD(0) == crowd[CROWD - 1]);
    TEST_ASSERT(CHILD(CROWD / 2 - 1) == crowd[1]);
    for (int i = 1; i < CROWD; i += 2)
        SEND(crowd[i], NULL, 0, MAKE_STR_TAG("stop"));
    TEST_ASSERT(wait_children(self, 0));

    // The pools are full again, from the crowd's exits
    uint64_t alone = spawn_and_lose_batches(self, 0);
    fprintf(stderr, "%d x %d children spawned and lost in %lu ms alone, %lu ms next to %d others\n", ROUNDS, BATCH,
            (unsigned long)alone, (unsigned long)crowded, CROWD);
    TEST_ASSERT(crowded < CROWD_SLOWDOWN * alone + TIMING_SLACK_MS);

    // Children that outlive their parent leave nothing behind in this list,
    // even once new children reuse the parent's slot
    Pid parent = SPAWN(short_parent, 1, self->pid);
    Message *msg = RECEIVE(MAKE_STR_TAG("orphans"));
    Pid orphans[ORPHANS];
    memcpy(orphans, msg->payload, sizeof orphans);
    MSG_FREE(msg);
    TEST_ASSERT(wait_children(self, 0));
    spawn_all(self, batch, ORPHANS);
    for (int i = 0; i < ORPHANS; ++i)
        SEND(orphans[i], NULL, 0, MAKE_STR_TAG("stop"));
    SLEEP(20);
    TEST_ASSERT(NB_CHILDREN() == ORPHANS);
    TEST_ASSERT(!is_child(self, parent));
    for (int i = 0; i < ORPHANS; ++i) {
        TEST_ASSERT(is_child(self, batch[i]));
        TEST_ASSERT(!is_child(self, orphans[i]));
        SEND(batch[i], NULL, 0, MAKE_STR_TAG("stop"));
    }
    TEST_ASSERT(wait_children(self, 0));

    TEST_PASS();
    EXIT_NORMAL();
}

CLPF_TEST_MAIN(entry)
