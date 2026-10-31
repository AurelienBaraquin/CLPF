#include "test_helpers.h"
#include <api/task.h>
#include <api/message.h>
#include <api/introspection.h>
#include <stdlib.h>
#include <string.h>

// The process table has room for 65 536 processes: only the slots in use may cost memory
#define MAX_IDLE_RSS_KIB 8192

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

TASK0(short_lived) {
    (void)self;
}

TASK(entry, int argc, char *argv[]) {
    (void)argc;
    (void)argv;

#ifndef CLPF_SANITIZED // Sanitizers map shadow memory for everything
    long kib = resident_kib();
    TEST_ASSERT(kib > 0 && kib < MAX_IDLE_RSS_KIB);
    if (kib >= MAX_IDLE_RSS_KIB)
        fprintf(stderr, "resident at start: %ld KiB\n", kib);
#endif

    // No valid pid is 0, and fresh slots work as before
    TEST_ASSERT(self->pid > 0);
    ProcessInfo info;
    TEST_ASSERT(PROCESS_INFO(0, &info) == -1);
    for (int i = 0; i < 100; ++i)
        TEST_ASSERT(SPAWN0(short_lived) > 0);

    Pid pids[16];
    size_t count = PROCESS_LIST(pids, 16);
    for (size_t i = 0; i < count && i < 16; ++i)
        TEST_ASSERT(pids[i] > 0);

    TEST_PASS();
}

CLPF_TEST_MAIN(entry)
