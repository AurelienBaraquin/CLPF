#include "test_helpers.h"
#include <api/task.h>
#include <api/message.h>
#include <api/timer.h>
#include <api/introspection.h>
#include <pthread.h>
#include <string.h>
#include <time.h>

#define NOISE        1000
#define RUNTIME_SENT 10
#define SPAM         5000
// Resumes allowed while waiting: the expected wakeup, plus one that was already queued
#define MAX_RUNS 10

static _Atomic long comparisons;

static bool counting_match(const Message *msg, Tag pattern) {
    atomic_fetch_add(&comparisons, 1);
    return clpf_match_same(msg, pattern);
}

// Messages sent from outside any process come from the runtime: each one wakes the receiver
static void *outside_sender(void *arg) {
    Pid pid = *(Pid *)arg;
    struct timespec pause = {0, 2L * 1000 * 1000};
    for (int i = 0; i < RUNTIME_SENT; ++i) {
        nanosleep(&pause, NULL);
        clpf_send(NULL, pid, NULL, 0, MAKE_STR_TAG("other"));
    }
    nanosleep(&pause, NULL);
    clpf_send(NULL, pid, NULL, 0, MAKE_STR_TAG("wanted"));
    return NULL;
}

TASK0(doubler) {
    while (1) {
        Message *msg = RECEIVE(NO_TAG);
        int value = *(int *)msg->payload * 2;
        REPLY(msg, &value, sizeof value);
        MSG_FREE(msg);
    }
}

TASK(spammer, Pid target) {
    for (int i = 0; i < SPAM; ++i)
        SEND(target, NULL, 0, MAKE_STR_TAG("spam"));
    SEND(target, NULL, 0, MAKE_STR_TAG("go"));
}

static void drop_all(Process *self, const char *tag) {
    Message *msg;
    while ((msg = FETCH(MAKE_STR_TAG(tag))) != NULL)
        MSG_FREE(msg);
}

TASK(entry, int argc, char *argv[]) {
    (void)argc;
    (void)argv;

    // A waiting RECEIVE looks at each message once, however often it wakes up
    for (int i = 0; i < NOISE; ++i)
        SEND(self->pid, NULL, 0, MAKE_STR_TAG("noise"));
    pthread_t thread;
    Pid pid = self->pid;
    TEST_ASSERT(pthread_create(&thread, NULL, outside_sender, &pid) == 0);
    Message *wanted = RECEIVE_IF(counting_match, MAKE_STR_TAG("wanted"));
    pthread_join(thread, NULL);
    TEST_ASSERT(wanted && MSG_IS(wanted, MAKE_STR_TAG("wanted")));
    MSG_FREE(wanted);
    long seen = atomic_load(&comparisons);
    TEST_ASSERT(seen >= NOISE && seen <= NOISE + 2 * (RUNTIME_SENT + 1));

    // Earlier messages are still there, in order, for the next RECEIVE
    TEST_ASSERT(FETCH(MAKE_STR_TAG("other")) != NULL);
    drop_all(self, "other");
    drop_all(self, "noise");
    TEST_ASSERT(FETCH(NO_TAG) == NULL);

    // Messages it does not wait for do not wake it up
    ProcessInfo before;
    TEST_ASSERT(PROCESS_INFO(self->pid, &before) == 0);
    SPAWN(spammer, 1, self->pid);
    MSG_FREE(RECEIVE(MAKE_STR_TAG("go")));
    ProcessInfo after;
    TEST_ASSERT(PROCESS_INFO(self->pid, &after) == 0);
    TEST_ASSERT(after.runs - before.runs <= MAX_RUNS);
    drop_all(self, "spam");

    // SLEEP is no longer cut short by every message
    SPAWN(spammer, 1, self->pid);
    TEST_ASSERT(PROCESS_INFO(self->pid, &before) == 0);
    SLEEP(30);
    TEST_ASSERT(PROCESS_INFO(self->pid, &after) == 0);
    TEST_ASSERT(after.runs - before.runs <= MAX_RUNS);
    drop_all(self, "spam");
    drop_all(self, "go");

    // Requests skip the backlog, which stays whole and in order
    for (int i = 0; i < NOISE; ++i)
        SEND(self->pid, &i, sizeof i, MAKE_STR_TAG("backlog"));
    Pid server = SPAWN0(doubler);
    for (int i = 0; i < 100; ++i) {
        Message *reply = REQUEST_REF(server, &i, sizeof i, 1000);
        TEST_ASSERT(reply && *(int *)reply->payload == 2 * i);
        MSG_FREE(reply);
    }
    for (int i = 0; i < NOISE; ++i) {
        Message *old = FETCH(MAKE_STR_TAG("backlog"));
        TEST_ASSERT(old && *(int *)old->payload == i);
        MSG_FREE(old);
    }

    TEST_PASS();
}

CLPF_TEST_MAIN(entry)
