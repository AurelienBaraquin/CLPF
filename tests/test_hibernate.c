#include "test_helpers.h"
#include <api/task.h>
#include <api/message.h>
#include <api/monitor.h>
#include <api/registry.h>
#include <api/link.h>
#include <api/timer.h>
#include <api/memory.h>
#include <api/introspection.h>
#include <memory/stack.h>
#include <stdlib.h>
#include <string.h>

#define ROUNDS 5
#ifdef CLPF_SANITIZED
#define CROWD 200
#else
#define CROWD 3000
#endif

// Counts the requests it answers; its state lives in its arguments only
TASK(counter, Pid boss, int total) {
    Message *msg = RECEIVE(MAKE_ATOM_TAG("count"));
    MSG_FREE(msg);
    ++total;
    SEND(boss, &total, sizeof total, MAKE_ATOM_TAG("counted"));
    HIBERNATE(counter, 2, boss, total);
}

// Fills its heap, frees it, then hibernates
TASK(heavy, Pid boss) {
    static const size_t chunk = (size_t)64 * 1024;
    void *blocks[16];
    for (int i = 0; i < 16; ++i)
        blocks[i] = ALLOC(chunk);
    for (int i = 0; i < 16; ++i)
        FREE(blocks[i]);
    SEND(boss, NULL, 0, MAKE_ATOM_TAG("filled"));
    HIBERNATE(counter, 2, boss, 0);
}

TASK0(sleeper) {
    HIBERNATE0(sleeper);
}

TASK0(waiter) {
    MSG_FREE(RECEIVE(MAKE_ATOM_TAG("never")));
}

#ifndef CLPF_SANITIZED // Sanitizers map shadow memory: resident sizes mean nothing there
static long resident_kib(void) {
    FILE *status = fopen("/proc/self/status", "r");
    if (!status)
        return -1;
    char line[256];
    long kib = -1;
    while (fgets(line, sizeof line, status)) {
        if (strncmp(line, "VmRSS:", 6) == 0)
            kib = strtol(line + 6, NULL, 10);
    }
    fclose(status);
    return kib;
}
#endif

TASK(entry, int argc, char *argv[]) {
    (void)argc;
    (void)argv;

    // It starts again in the function given, with its arguments, for each message
    Pid pid = SPAWN(counter, 2, self->pid, 0);
    TEST_ASSERT(REGISTER("hibernating::counter", pid) == 0);
    MONITOR(pid);
    for (int i = 1; i <= ROUNDS; ++i) {
        SEND(pid, NULL, 0, MAKE_ATOM_TAG("count"));
        Message *counted = RECEIVE(MAKE_ATOM_TAG("counted"));
        TEST_ASSERT(*(int *)counted->payload == i);
        MSG_FREE(counted);
    }

    // Asleep without a stack, it keeps its pid, name, monitors and mailbox
    SLEEP(20);
    ProcessInfo info;
    TEST_ASSERT(PROCESS_INFO(pid, &info) == 0 && info.state == PROCESS_WAITING && info.monitored_by == 1);
    TEST_ASSERT(WHEREIS("hibernating::counter") == pid);
    SEND(pid, NULL, 0, MAKE_ATOM_TAG("count"));
    SEND(pid, NULL, 0, MAKE_ATOM_TAG("count"));
    for (int i = ROUNDS + 1; i <= ROUNDS + 2; ++i) {
        Message *counted = RECEIVE(MAKE_ATOM_TAG("counted"));
        TEST_ASSERT(*(int *)counted->payload == i);
        MSG_FREE(counted);
    }

    // It can still be killed
    KILL(pid);
    Message *down = RECEIVE_TIMEOUT(MAKE_DOWN_TAG(pid), 1000);
    TEST_ASSERT(down && ((DownInfo *)down->payload)->reason == EXIT_REASON_KILLED);
    MSG_FREE(down);

    // Hibernating gives back the heap pools it no longer uses
    pid = SPAWN(heavy, 1, self->pid);
    MSG_FREE(RECEIVE(MAKE_ATOM_TAG("filled")));
    SLEEP(20);
    TEST_ASSERT(PROCESS_INFO(pid, &info) == 0 && info.heap_bytes < (size_t)64 * 1024);
    KILL(pid);

    // Hibernating processes cost less memory than waiting ones: no stack. Each
    // keeps its heap's first pool, which holds the allocator (about 3 pages)
#ifndef CLPF_SANITIZED
    long before = resident_kib();
    for (int i = 0; i < CROWD; ++i) {
        SPAWN0(waiter);
        REDUCE(50);
    }
    SLEEP(100);
    long waiting = resident_kib() - before;
    before = resident_kib();
    for (int i = 0; i < CROWD; ++i) {
        SPAWN0(sleeper);
        REDUCE(50);
    }
    SLEEP(100);
    long hibernating = resident_kib() - before;
    TEST_ASSERT(hibernating < waiting);
    if (hibernating >= waiting)
        fprintf(stderr, "waiting: %ld KiB, hibernating: %ld KiB\n", waiting, hibernating);
#endif

    TEST_PASS();
}

CLPF_TEST_MAIN(entry)
