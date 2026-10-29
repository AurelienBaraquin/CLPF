#include "test_helpers.h"
#include <api/task.h>
#include <api/message.h>
#include <stdint.h>

static void send_self(Process *self, Tag tag) {
    int value = 1;
    SEND(self->pid, &value, sizeof value, tag);
}

static int take_count(Process *self, MessageMatch match, Tag pattern) {
    int count = 0;
    Message *msg;
    while ((msg = FETCH_IF(match, pattern)) != NULL) {
        ++count;
        MSG_FREE(msg);
    }
    return count;
}

TASK(entry, int argc, char *argv[]) {
    (void)argc;
    (void)argv;

    // An empty message is a message: FETCH returns it, not NULL
    SEND(self->pid, NULL, 0, MAKE_STR_TAG("empty"));
    Message *empty = FETCH(MAKE_STR_TAG("empty"));
    TEST_ASSERT(empty != NULL);
    TEST_ASSERT(empty->payload == NULL && empty->size == 0);
    TEST_ASSERT(empty->sender_pid == self->pid);
    MSG_FREE(empty);
    TEST_ASSERT(FETCH(MAKE_STR_TAG("empty")) == NULL);

    // Payloads are copied and aligned for any type
    double value = 2.5;
    SEND(self->pid, &value, sizeof value, MAKE_INT_TAG(7));
    Message *copy = RECEIVE(MAKE_INT_TAG(7));
    TEST_ASSERT((uintptr_t)copy->payload % 16 == 0);
    TEST_ASSERT(*(double *)copy->payload == 2.5);
    MSG_FREE(copy);

    // Matchers
    send_self(self, MAKE_STR_TAG("log::info"));
    send_self(self, MAKE_STR_TAG("log::error"));
    send_self(self, MAKE_STR_TAG("metrics"));
    TEST_ASSERT(take_count(self, TM_PREFIX, MAKE_STR_TAG("log::")) == 2);

    for (int i = -5; i <= 5; ++i)
        send_self(self, MAKE_INT_TAG(i));
    TEST_ASSERT(take_count(self, TM_IN_RANGE, MAKE_RANGE_TAG(-2, 1)) == 4);
    TEST_ASSERT(take_count(self, TM_GTE, MAKE_INT_TAG(3)) == 3);
    TEST_ASSERT(take_count(self, TM_LTE, MAKE_INT_TAG(-4)) == 2);
    TEST_ASSERT(take_count(self, TM_KIND, MAKE_TAG_KIND(TAG_KIND_INTEGER)) == 2);

    send_self(self, MAKE_INTERNAL_TAG(9));
    TEST_ASSERT(take_count(self, TM_SAME, MAKE_INTERNAL_TAG(8)) == 0);
    TEST_ASSERT(take_count(self, TM_SAME, MAKE_INTERNAL_TAG(9)) == 1);

    // NO_TAG takes whatever is left, oldest first
    Message *rest = RECEIVE(NO_TAG);
    TEST_ASSERT(MSG_IS(rest, MAKE_STR_TAG("metrics")));
    MSG_FREE(rest);
    TEST_ASSERT(FETCH(NO_TAG) == NULL);

    // Selective receive keeps the other messages in order
    send_self(self, MAKE_INT_TAG(1));
    send_self(self, MAKE_INT_TAG(2));
    send_self(self, MAKE_INT_TAG(3));
    MSG_FREE(RECEIVE(MAKE_INT_TAG(2)));
    Message *first = RECEIVE(NO_TAG);
    Message *second = RECEIVE(NO_TAG);
    TEST_ASSERT(first->tag.int_value == 1 && second->tag.int_value == 3);
    MSG_FREE(first);
    MSG_FREE(second);

    TEST_PASS();
    EXIT_NORMAL();
}

CLPF_TEST_MAIN(entry)
