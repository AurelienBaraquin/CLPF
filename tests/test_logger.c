#include "test_helpers.h"
#include <api/task.h>
#include <api/message.h>
#include <api/io.h>
#include <api/log.h>
#include <string.h>
#include <unistd.h>

// Reads what reached stderr until it contains `expected`, or gives up after a second
static bool read_until(Process *self, int fd, char *buffer, size_t capacity, const char *expected) {
    size_t length = strlen(buffer);
    while (!strstr(buffer, expected)) {
        if (IO_WAIT_TIMEOUT(fd, IO_READ, 1000) != IO_READ)
            return false;
        ssize_t got = read(fd, buffer + length, capacity - 1 - length);
        if (got <= 0)
            return false;
        length += (size_t)got;
        buffer[length] = '\0';
    }
    return true;
}

TASK(logger, Pid boss) {
    LOG_DEBUG("hidden %d", 1);
    LOG_INFO("hidden %d", 2);
    LOG_WARN("disk at %d%%", 91);
    LOG_ERROR("lost %s", "connection");
    SEND(boss, NULL, 0, MAKE_STR_TAG("logged"));
}

TASK(entry, int argc, char *argv[]) {
    (void)argc;
    (void)argv;

    // The test runs with CLPF_LOG_LEVEL=Debug
    TEST_ASSERT(clpf_log_level() == LOG_LEVEL_DEBUG);

    int fds[2];
    TEST_ASSERT(pipe(fds) == 0);
    int saved_stderr = dup(STDERR_FILENO);
    TEST_ASSERT(saved_stderr >= 0 && dup2(fds[1], STDERR_FILENO) == STDERR_FILENO);
    if (saved_stderr < 0)
        return;

    clpf_set_log_level(LOG_LEVEL_WARN);
    TEST_ASSERT(clpf_log_level() == LOG_LEVEL_WARN);
    Pid pid = SPAWN_NAMED(logger, "disk_watch", 1, self->pid);
    MSG_FREE(RECEIVE(MAKE_STR_TAG("logged")));

    char buffer[1024] = {0};
    bool complete = read_until(self, fds[0], buffer, sizeof buffer, "lost connection\n");
    dup2(saved_stderr, STDERR_FILENO);
    TEST_ASSERT(complete);

    // Filtered levels never reach the output, the others keep their order
    TEST_ASSERT(!strstr(buffer, "hidden"));
    char *warn = strstr(buffer, " WARN  ");
    char *error = strstr(buffer, " ERROR ");
    TEST_ASSERT(warn && error && warn < error);
    char label[64];
    snprintf(label, sizeof label, "disk_watch<%ld>: disk at 91%%\n", pid);
    TEST_ASSERT(strstr(buffer, label));

    clpf_set_log_level(LOG_LEVEL_NONE);
    TEST_ASSERT(LOG_ERROR("silent") == 0);

    close(fds[0]);
    close(fds[1]);
    close(saved_stderr);
    TEST_PASS();
}

CLPF_TEST_MAIN(entry)
