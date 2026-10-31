#include "test_helpers.h"
#include <api/task.h>
#include <api/message.h>
#include <api/timer.h>
#include <api/io.h>
#include <utils/time.h>
#include <string.h>
#include <unistd.h>

// Built with clpf::auto_reduce, and run with CLPF_WATCHDOG_MS=50
#define SPIN_MS 300
#define FLOOD   2000

__attribute__((noinline)) static uint64_t step(uint64_t value) {
    return value * 2654435761u + 1;
}

// Never calls the runtime itself: only its function calls give the CPU away
TASK(spinner, Pid boss) {
    uint64_t end = now_ms() + SPIN_MS, value = 0;
    while (now_ms() < end)
        value = step(value);
    SEND(boss, &value, sizeof value, MAKE_ATOM_TAG("spun"));
}

// A matcher of the program's own: senders also run it, under the receiver's lock
static bool wanted(const Message *msg, Tag pattern) {
    return step((uint64_t)msg->size) != 0 && msg->tag.kind == pattern.kind && msg->tag.str_value == pattern.str_value;
}

TASK(flooder, Pid target) {
    for (int i = 0; i < FLOOD; ++i)
        SEND(target, NULL, 0, MAKE_ATOM_TAG("noise"));
    SEND(target, NULL, 0, MAKE_ATOM_TAG("wanted"));
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

    // A loop that only calls functions gives the CPU away: the watchdog stays quiet
    SPAWN(spinner, 1, self->pid);
    MSG_FREE(RECEIVE(MAKE_ATOM_TAG("spun")));

    // Matchers run by senders under a lock never give the CPU away there
    SPAWN(flooder, 1, self->pid);
    Message *msg = RECEIVE_IF_TIMEOUT(wanted, MAKE_ATOM_TAG("wanted"), 5000);
    TEST_ASSERT(msg);
    MSG_FREE(msg);

    // The watchdog had nothing to report
    SLEEP(20);
    static char output[4096];
    dup2(saved_stderr, STDERR_FILENO);
    close(fds[1]);
    ssize_t length = read(fds[0], output, sizeof output - 1);
    TEST_ASSERT(length <= 0 || !strstr(output, "watchdog"));

    close(fds[0]);
    close(saved_stderr);
    TEST_PASS();
}

CLPF_TEST_MAIN(entry)
