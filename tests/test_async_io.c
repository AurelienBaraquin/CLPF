#include "test_helpers.h"
#include <api/task.h>
#include <api/message.h>
#include <api/timer.h>
#include <api/link.h>
#include <api/monitor.h>
#include <api/io.h>
#include <unistd.h>

TASK(late_writer, int fd, int delay_ms) {
    SLEEP((uint64_t)delay_ms);
    TEST_ASSERT(write(fd, "x", 1) == 1);
}

TASK(watcher, int fd, Pid boss) {
    TEST_ASSERT(IO_WATCH(fd, IO_READ) == 0);
    SEND(boss, NULL, 0, MAKE_STR_TAG("watching"));
    MSG_FREE(RECEIVE(MAKE_STR_TAG("never")));
}

TASK(entry, int argc, char *argv[]) {
    (void)argc;
    (void)argv;

    int fds[2];
    TEST_ASSERT(pipe(fds) == 0);
    char byte;

    // IO_WAIT blocks the process until the data comes, without blocking the scheduler
    SPAWN(late_writer, 2, fds[1], 20);
    TEST_ASSERT(IO_WAIT(fds[0], IO_READ) == IO_READ);
    TEST_ASSERT(read(fds[0], &byte, 1) == 1);

    // A pipe with room is writable at once
    TEST_ASSERT(IO_WAIT(fds[1], IO_WRITE) == IO_WRITE);

    // A timeout returns 0 and leaves nothing behind
    TEST_ASSERT(IO_WAIT_TIMEOUT(fds[0], IO_READ, 20) == 0);
    TEST_ASSERT(write(fds[1], "y", 1) == 1);
    SLEEP(20);
    TEST_ASSERT(FETCH(NO_TAG) == NULL);
    TEST_ASSERT(read(fds[0], &byte, 1) == 1);

    // IO_WATCH reports readiness with a message
    TEST_ASSERT(IO_WATCH(fds[0], IO_READ) == 0);
    TEST_ASSERT(write(fds[1], "z", 1) == 1);
    Message *ready = RECEIVE_TIMEOUT(MAKE_FD_TAG(fds[0]), 1000);
    TEST_ASSERT(ready && ready->size == sizeof(int) && *(int *)ready->payload == IO_READ);
    MSG_FREE(ready);
    TEST_ASSERT(read(fds[0], &byte, 1) == 1);

    // An unwatched fd sends nothing
    TEST_ASSERT(IO_WATCH(fds[0], IO_READ) == 0);
    TEST_ASSERT(IO_UNWATCH(fds[0]) == 0);
    TEST_ASSERT(IO_UNWATCH(fds[0]) == -1);
    TEST_ASSERT(write(fds[1], "w", 1) == 1);
    SLEEP(20);
    TEST_ASSERT(FETCH(NO_TAG) == NULL);
    TEST_ASSERT(read(fds[0], &byte, 1) == 1);

    // One watcher per fd; a dead watcher has released its fd by the time its
    // monitors hear of it, so they may reuse it at once
    Pid other = SPAWN(watcher, 2, fds[0], self->pid);
    MSG_FREE(RECEIVE(MAKE_STR_TAG("watching")));
    TEST_ASSERT(IO_WATCH(fds[0], IO_READ) == -1);
    MONITOR(other);
    KILL(other);
    MSG_FREE(RECEIVE(MAKE_DOWN_TAG(other)));
    TEST_ASSERT(IO_WATCH(fds[0], IO_READ) == 0);
    TEST_ASSERT(IO_UNWATCH(fds[0]) == 0);

    // A bad fd is an error
    TEST_ASSERT(IO_WAIT(-1, IO_READ) == -1);

    close(fds[0]);
    close(fds[1]);
    TEST_PASS();
}

CLPF_TEST_MAIN(entry)
