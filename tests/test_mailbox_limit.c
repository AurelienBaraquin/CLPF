#include "test_helpers.h"
#include <api/task.h>
#include <api/message.h>
#include <api/monitor.h>
#include <api/link.h>
#include <api/timer.h>

#define LIMIT 3

TASK0(waiter) {
    MSG_FREE(RECEIVE(MAKE_STR_TAG("never")));
}

// Lets its mailbox fill up while it sleeps, then counts what got in
TASK(consumer, Pid producer) {
    Pid child = SPAWN_OPTS(waiter, .monitor = true);
    SEND(producer, &child, sizeof child, MAKE_STR_TAG("child"));
    SLEEP(100);

    int counts[2] = {0, 0}; // data messages, DOWN messages
    Message *msg;
    while ((msg = FETCH(NO_TAG))) {
        counts[msg->tag.kind == TAG_KIND_DOWN]++;
        MSG_FREE(msg);
    }
    SEND(producer, counts, sizeof counts, MAKE_STR_TAG("counts"));

    // Room again: new messages get in
    MSG_FREE(RECEIVE(MAKE_STR_TAG("x")));
    SEND(producer, NULL, 0, MAKE_STR_TAG("got"));
}

TASK(entry, int argc, char *argv[]) {
    (void)argc;
    (void)argv;

    Funcall_t call;
    funcall_init(&call, consumer, 1, self->pid);
    Pid pid = clpf_spawn(self, &call, (SpawnOptions){.mailbox_limit = LIMIT});

    Message *msg = RECEIVE(MAKE_STR_TAG("child"));
    Pid child = *(Pid *)msg->payload;
    MSG_FREE(msg);

    for (int i = 0; i < LIMIT; ++i)
        TEST_ASSERT(SEND(pid, NULL, 0, MAKE_STR_TAG("x")) == 0);
    TEST_ASSERT(SEND(pid, NULL, 0, MAKE_STR_TAG("x")) == SEND_FULL);

    // Messages from the runtime are never refused: this DOWN gets in
    KILL(child);

    msg = RECEIVE(MAKE_STR_TAG("counts"));
    int *counts = msg->payload;
    TEST_ASSERT(counts[0] == LIMIT && counts[1] == 1);
    MSG_FREE(msg);

    TEST_ASSERT(SEND(pid, NULL, 0, MAKE_STR_TAG("x")) == 0);
    MSG_FREE(RECEIVE(MAKE_STR_TAG("got")));

    TEST_PASS();
}

CLPF_TEST_MAIN(entry)
