#include "test_helpers.h"
#include <api/task.h>
#include <api/message.h>
#include <api/memory.h>
#include <api/monitor.h>
#include <api/supervisor.h>
#include <api/timer.h>

#define LIMIT ((size_t)64 * 1024)
#define CHUNK 1024

TASK0(hungry) {
    while (1) {
        TEST_ASSERT(ALLOC(CHUNK) != NULL); // never NULL: the process ends instead
        REDUCE(1);
    }
}

TASK0(deaf) {
    MSG_FREE(RECEIVE(MAKE_ATOM_TAG("never")));
}

TASK(free_eater, Pid boss) {
    void *big = ALLOC((size_t)1024 * 1024);
    TEST_ASSERT(big != NULL);
    FREE(big);
    SEND(boss, NULL, 0, MAKE_ATOM_TAG("ate"));
}

static unsigned int down_reason(Process *self, Pid pid) {
    Message *down = RECEIVE_TIMEOUT(MAKE_DOWN_TAG(pid), 5000);
    TEST_ASSERT(down);
    unsigned int reason = down ? ((DownInfo *)down->payload)->reason : EXIT_REASON_NORMAL;
    MSG_FREE(down);
    return reason;
}

TASK(entry, int argc, char *argv[]) {
    (void)argc;
    (void)argv;

    // Allocating past the limit ends the process
    Pid pid = SPAWN_OPTS(hungry, .max_heap_size = LIMIT, .monitor = true);
    TEST_ASSERT(down_reason(self, pid) == EXIT_REASON_HEAP_LIMIT);

    // So does receiving past it: the sender sees the message refused
    pid = SPAWN_OPTS(deaf, .max_heap_size = LIMIT, .monitor = true);
    static char chunk[CHUNK];
    int refused = 0;
    for (int i = 0; i < 1000 && refused == 0; ++i)
        refused = SEND(pid, chunk, sizeof chunk, MAKE_ATOM_TAG("flood")) != 0;
    TEST_ASSERT(refused);
    TEST_ASSERT(down_reason(self, pid) == EXIT_REASON_HEAP_LIMIT);

    // Without a limit, a heap grows as needed
    SPAWN(free_eater, 1, self->pid);
    Message *ate = RECEIVE_TIMEOUT(MAKE_ATOM_TAG("ate"), 1000);
    TEST_ASSERT(ate);
    MSG_FREE(ate);

    // Supervisor children get the limit too
    Pid sup = SPAWN_SUPERVISOR("hungry_sup", SUP_ONE_FOR_ONE, 0, 0);
    Pid child = SUP_ADD_CHILD_OPTS(sup, RES_TEMPORARY, hungry, .name = "hungry", .max_heap_size = LIMIT);
    TEST_ASSERT(child != INVALID_PID);
    MONITOR(child);
    TEST_ASSERT(down_reason(self, child) == EXIT_REASON_HEAP_LIMIT);

    TEST_PASS();
}

CLPF_TEST_MAIN(entry)
