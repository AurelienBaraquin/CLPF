#pragma once

#include <api/base.h>
#include <stdatomic.h>
#include <stdio.h>

// A CLPF test is a regular CLPF program: the entry task runs the scenario and
// reports through TEST_ASSERT / TEST_PASS; the runtime stops when it ends.
// The process exit code is 0 only if TEST_PASS was reached without failure.

typedef enum {
    TEST_RUNNING,
    TEST_PASSED,
    TEST_FAILED,
} TestStatus;

extern _Atomic TestStatus test_status;

#define TEST_ASSERT(cond)                                                     \
    do {                                                                      \
        if (!(cond)) {                                                        \
            fprintf(stderr, "%s:%d: assertion failed: %s\n",                  \
                    __FILE__, __LINE__, #cond);                               \
            atomic_store(&test_status, TEST_FAILED);                          \
        }                                                                     \
    } while (0)

#define TEST_PASS()                                                           \
    do {                                                                      \
        TestStatus clpf_running = TEST_RUNNING;                                  \
        atomic_compare_exchange_strong(&test_status, &clpf_running, TEST_PASSED);\
    } while (0)

#define CLPF_TEST_MAIN(entry)                                                 \
    _Atomic TestStatus test_status = TEST_RUNNING;                            \
    int main(int argc, char *argv[]) {                                        \
        clpf_main(entry, argc, argv);                                       \
        return atomic_load(&test_status) == TEST_PASSED ? 0 : 1;              \
    }
