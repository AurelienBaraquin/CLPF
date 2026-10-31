#include "test_helpers.h"
#include <api/task.h>
#include <api/message.h>
#include <api/timer.h>
#include <memory/safe_heap.h>
#include <memory/stack.h>

// The test runs with CLPF_POOL_PREWARM=64
#define PREWARMED 64
#define SPAWNED   40
// The runtime's own processes may hold a few
#define RUNTIME_PROCESSES 8

TASK0(waiter) {
    MSG_FREE(RECEIVE(MAKE_ATOM_TAG("never")));
}

TASK(entry, int argc, char *argv[]) {
    (void)argc;
    (void)argv;

    size_t stacks = stack_pool_count();
    size_t heaps = safe_heap_pool_count();
    TEST_ASSERT(stacks >= PREWARMED - RUNTIME_PROCESSES && stacks <= PREWARMED);
    TEST_ASSERT(heaps >= PREWARMED - RUNTIME_PROCESSES && heaps <= PREWARMED);

    // New processes take their stack and heap from the pools, without mapping any
    for (int i = 0; i < SPAWNED; ++i)
        SPAWN0(waiter);
    SLEEP(50); // A stack is taken when a process first runs
    // Runtime processes ending meanwhile (the launcher) give theirs back
    TEST_ASSERT(stack_pool_count() >= stacks - SPAWNED && stack_pool_count() <= stacks - SPAWNED + RUNTIME_PROCESSES);
    TEST_ASSERT(safe_heap_pool_count() >= heaps - SPAWNED &&
                safe_heap_pool_count() <= heaps - SPAWNED + RUNTIME_PROCESSES);

    TEST_PASS();
}

CLPF_TEST_MAIN(entry)
