#include "test_helpers.h"
#include <api/task.h>
#include <api/message.h>
#include <api/io.h>
#include <api/timer.h>
#include <utils/spinlock.h>
#include <utils/time.h>
#include <string.h>
#include <unistd.h>

// The test runs with CLPF_WATCHDOG_MS=50
#define BUSY_MS 300

// Never calls the runtime while it runs: nothing can preempt it
TASK(busy_loop, Pid boss) {
    uint64_t end = now_ms() + BUSY_MS;
    while (now_ms() < end)
        cpu_relax();
    SEND(boss, NULL, 0, MAKE_STR_TAG("done"));
}

static size_t occurrences(const char *text, const char *word) {
    size_t count = 0;
    for (const char *at = strstr(text, word); at; at = strstr(at + 1, word))
        ++count;
    return count;
}

TASK(entry, int argc, char *argv[]) {
    (void)argc;
    (void)argv;

    int fds[2];
    TEST_ASSERT(pipe(fds) == 0);
    int saved_stderr = dup(STDERR_FILENO);
    TEST_ASSERT(saved_stderr >= 0 && dup2(fds[1], STDERR_FILENO) == STDERR_FILENO);
    if (saved_stderr < 0)
        return;

    Pid pid = SPAWN_NAMED(busy_loop, "spinner", 1, self->pid);
    MSG_FREE(RECEIVE(MAKE_STR_TAG("done")));
    SLEEP(20);

    // Process stacks are small: keep the output off the stack
    static char output[4096];
    if (IO_WAIT_TIMEOUT(fds[0], IO_READ, 1000) == IO_READ)
        TEST_ASSERT(read(fds[0], output, sizeof output - 1) > 0);
    dup2(saved_stderr, STDERR_FILENO);

    // Reported once, with its name and pid, however long it runs
    char expected[64];
    snprintf(expected, sizeof expected, "'spinner' (pid %ld)", pid);
    TEST_ASSERT(strstr(output, expected));
    TEST_ASSERT(occurrences(output, "watchdog") == 1);

    close(fds[0]);
    close(fds[1]);
    close(saved_stderr);
    TEST_PASS();
}

CLPF_TEST_MAIN(entry)
