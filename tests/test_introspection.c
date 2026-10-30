#include "test_helpers.h"
#include <api/task.h>
#include <api/message.h>
#include <api/monitor.h>
#include <api/registry.h>
#include <api/timer.h>
#include <api/io.h>
#include <api/introspection.h>
#include <signal.h>
#include <string.h>
#include <unistd.h>

#define LIST_MAX 64

TASK0(waiter) {
    MSG_FREE(RECEIVE(MAKE_STR_TAG("never")));
}

static bool listed(const Pid *pids, size_t count, Pid pid) {
    for (size_t i = 0; i < count; ++i) {
        if (pids[i] == pid)
            return true;
    }
    return false;
}

// Reads what reached fd until it contains `expected`, or gives up after a second
static bool read_until(Process *self, int fd, char *buffer, size_t capacity, const char *expected) {
    size_t length = strlen(buffer);
    while (!strstr(buffer, expected) && length + 1 < capacity) {
        if (IO_WAIT_TIMEOUT(fd, IO_READ, 1000) != IO_READ)
            return false;
        ssize_t got = read(fd, buffer + length, capacity - 1 - length);
        if (got <= 0)
            return false;
        length += (size_t)got;
        buffer[length] = '\0';
    }
    return strstr(buffer, expected) != NULL;
}

TASK(entry, int argc, char *argv[]) {
    (void)argc;
    (void)argv;

    Pid pid = SPAWN0_PRIO_NAMED(waiter, "inspected", PROCESS_PRIORITY_HIGH);
    TEST_ASSERT(REGISTER("intro::waiter", pid) == 0);
    MONITOR(pid);
    for (int i = 0; i < 3; ++i)
        SEND(pid, NULL, 0, MAKE_STR_TAG("ignored"));
    SLEEP(20);

    // What a process is doing, seen from outside
    ProcessInfo info;
    TEST_ASSERT(PROCESS_INFO(pid, &info) == 0);
    TEST_ASSERT(info.pid == pid);
    TEST_ASSERT(strcmp(info.name, "inspected") == 0);
    TEST_ASSERT(strcmp(info.registered_name, "intro::waiter") == 0);
    TEST_ASSERT(info.state == PROCESS_WAITING);
    TEST_ASSERT(info.priority == PROCESS_PRIORITY_HIGH);
    TEST_ASSERT(info.scheduler >= 0);
    TEST_ASSERT(info.queued == 3);
    TEST_ASSERT(info.heap_bytes > 0);
    TEST_ASSERT(info.runs >= 1);
    TEST_ASSERT(info.monitored_by == 1);
    TEST_ASSERT(!info.system);

    TEST_ASSERT(PROCESS_INFO(self->pid, &info) == 0 && info.state == PROCESS_RUNNING);
    TEST_ASSERT(PROCESS_INFO(INVALID_PID, &info) == -1);

    // Every process: this one, the waiter, and the system ones
    Pid pids[LIST_MAX];
    size_t count = PROCESS_LIST(pids, LIST_MAX);
    TEST_ASSERT(count >= 3 && count <= LIST_MAX);
    TEST_ASSERT(listed(pids, count, pid) && listed(pids, count, self->pid) &&
                listed(pids, count, WHEREIS("server::io")));
    TEST_ASSERT(PROCESS_LIST(pids, 1) == count);

    // A table of all processes, on demand or on SIGUSR1
    char *text = NULL;
    size_t size = 0;
    FILE *out = open_memstream(&text, &size);
    TEST_ASSERT(out);
    clpf_dump(out);
    fclose(out);
    TEST_ASSERT(text && strstr(text, "inspected") && strstr(text, "intro::waiter") && strstr(text, "server::io"));
    free(text);

    int fds[2];
    TEST_ASSERT(pipe(fds) == 0);
    int saved_stderr = dup(STDERR_FILENO);
    TEST_ASSERT(saved_stderr >= 0 && dup2(fds[1], STDERR_FILENO) == STDERR_FILENO);
    if (saved_stderr < 0)
        return;
    TEST_ASSERT(kill(getpid(), SIGUSR1) == 0);
    // Process stacks are small: keep the dump off the stack
    static char buffer[16384];
    bool dumped = read_until(self, fds[0], buffer, sizeof buffer, "intro::waiter");
    dup2(saved_stderr, STDERR_FILENO);
    TEST_ASSERT(dumped);

    close(fds[0]);
    close(fds[1]);
    close(saved_stderr);
    TEST_PASS();
}

CLPF_TEST_MAIN(entry)
