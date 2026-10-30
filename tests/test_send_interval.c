#include "test_helpers.h"
#include <api/task.h>
#include <api/message.h>
#include <api/timer.h>
#include <utils/time.h>

#define PERIOD_MS 10
#define TICKS     5

TASK(entry, int argc, char *argv[]) {
    (void)argc;
    (void)argv;

    // A periodic timer fires again and again, each time with a copy of its message
    int value = 7;
    uint64_t start = now_ms();
    TimerId ticker = SEND_INTERVAL(self->pid, PERIOD_MS, &value, sizeof value, MAKE_STR_TAG("tick"));
    TEST_ASSERT(ticker != 0);
    for (int i = 0; i < TICKS; ++i) {
        Message *tick = RECEIVE(MAKE_STR_TAG("tick"));
        TEST_ASSERT(*(int *)tick->payload == 7);
        MSG_FREE(tick);
    }
    TEST_ASSERT(now_ms() - start >= (uint64_t)PERIOD_MS * TICKS);

    // ...until it is cancelled
    TEST_ASSERT(CANCEL_TIMER(ticker) == 0);
    while (FETCH(MAKE_STR_TAG("tick")))
        ; // a tick may have fired just before the cancel
    SLEEP((uint64_t)5 * PERIOD_MS);
    TEST_ASSERT(FETCH(MAKE_STR_TAG("tick")) == NULL);
    TEST_ASSERT(CANCEL_TIMER(ticker) == -1);

    TEST_ASSERT(SEND_INTERVAL(self->pid, 0, NULL, 0, MAKE_STR_TAG("never")) == 0);

    TEST_PASS();
}

CLPF_TEST_MAIN(entry)
