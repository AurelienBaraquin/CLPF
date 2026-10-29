#include "test_helpers.h"
#include <api/task.h>
#include <api/message.h>
#include <api/timer.h>
#include <utils/time.h>

#define DELAY_MS UINT64_C(50)
#define SLACK_MS UINT64_C(500) // generous upper bound for loaded machines and sanitizers

TASK(late_sender, Pid target) {
    SLEEP(DELAY_MS);
    SEND(target, NULL, 0, MAKE_STR_TAG("late"));
    EXIT_NORMAL();
}

TASK(entry, int argc, char *argv[]) {
    (void)argc;
    (void)argv;

    // SEND_AFTER delivers once the delay has passed, with a copy of the data and tag
    uint64_t start = now_ms();
    char tag[16] = "tick";
    int value = 7;
    TEST_ASSERT(SEND_AFTER(self->pid, DELAY_MS, &value, sizeof value, MAKE_STR_TAG(tag)) != 0);
    tag[0] = 'x';
    value = 0;
    Message *tick = RECEIVE(MAKE_STR_TAG("tick"));
    uint64_t waited = now_ms() - start;
    TEST_ASSERT(waited >= DELAY_MS && waited < DELAY_MS + SLACK_MS);
    TEST_ASSERT(*(int *)tick->payload == 7);
    TEST_ASSERT(tick->sender_pid == self->pid);
    MSG_FREE(tick);

    // A cancelled timer never fires
    TimerId cancelled = SEND_AFTER(self->pid, DELAY_MS, NULL, 0, MAKE_STR_TAG("cancelled"));
    TEST_ASSERT(CANCEL_TIMER(cancelled) == 0);
    TEST_ASSERT(CANCEL_TIMER(cancelled) == -1);

    // RECEIVE_TIMEOUT gives up with NULL...
    start = now_ms();
    TEST_ASSERT(RECEIVE_TIMEOUT(MAKE_STR_TAG("nothing"), 2 * DELAY_MS) == NULL);
    waited = now_ms() - start;
    TEST_ASSERT(waited >= 2 * DELAY_MS && waited < 2 * DELAY_MS + SLACK_MS);
    TEST_ASSERT(FETCH(MAKE_STR_TAG("cancelled")) == NULL);

    // ...or returns the message as soon as it arrives
    SPAWN(late_sender, 1, self->pid);
    start = now_ms();
    Message *late = RECEIVE_TIMEOUT(MAKE_STR_TAG("late"), 10 * DELAY_MS);
    TEST_ASSERT(late != NULL);
    TEST_ASSERT(now_ms() - start < 10 * DELAY_MS);
    MSG_FREE(late);

    // A zero timeout only polls, and no timeout message is ever left behind
    TEST_ASSERT(RECEIVE_TIMEOUT(MAKE_STR_TAG("nothing"), 0) == NULL);
    SLEEP(DELAY_MS);
    TEST_ASSERT(FETCH(NO_TAG) == NULL);

    TEST_PASS();
    EXIT_NORMAL();
}

CLPF_TEST_MAIN(entry)
