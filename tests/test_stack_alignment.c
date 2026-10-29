#include "test_helpers.h"
#include <stdint.h>
#include <stdio.h>

// The System V ABI requires rsp + 8 to be 16-byte aligned on function entry.
// glibc's printf uses aligned SSE stores and crashes if a task breaks this.
TASK(entry, int argc, char *argv[]) {
    (void)argc;
    (void)argv;

    TEST_ASSERT((uintptr_t)__builtin_frame_address(0) % 16 == 0);

    char buffer[32];
    snprintf(buffer, sizeof buffer, "%.2f", 3.14159);
    TEST_ASSERT(buffer[0] == '3');

    TEST_PASS();
    EXIT_NORMAL();
}

CLPF_TEST_MAIN(entry)
