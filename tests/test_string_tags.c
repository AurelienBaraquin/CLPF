#include "test_helpers.h"
#include <api/task.h>
#include <api/message.h>
#include <stdio.h>
#include <string.h>

// The tag string is built in a stack buffer that is gone once the sender exits
TASK(sender, Pid target, int id) {
    char tag[32];
    snprintf(tag, sizeof tag, "job::%d", id);
    SEND(target, NULL, 0, MAKE_STR_TAG(tag));

    snprintf(tag, sizeof tag, "worker_%d_up", id);
    NOTIFY(target, tag);
    memset(tag, 'x', sizeof tag - 1);
    EXIT_NORMAL();
}

TASK(entry, int argc, char *argv[]) {
    (void)argc;
    (void)argv;

    TEST_ASSERT(SPAWN(sender, 2, self->pid, 7) != INVALID_PID);

    // Notification names can be built at run time
    char name[32];
    snprintf(name, sizeof name, "worker_%d_up", 7);
    WAIT_NOTIF(name);

    // The tag travelled with the message: it does not point into the sender's stack
    Message *msg = FETCH(MAKE_STR_TAG("job::7"));
    TEST_ASSERT(msg != NULL);
    if (msg) {
        TEST_ASSERT(strcmp(msg->tag.str_value, "job::7") == 0);
        MSG_FREE(msg);
    }

    // A notification is not a plain string tag with the same text
    SEND(self->pid, NULL, 0, MAKE_STR_TAG("ready"));
    TEST_ASSERT(FETCH(MAKE_NOTIFY_TAG("ready")) == NULL);
    MSG_FREE(FETCH(MAKE_STR_TAG("ready")));

    TEST_PASS();
    EXIT_NORMAL();
}

CLPF_TEST_MAIN(entry)
