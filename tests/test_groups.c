#include "test_helpers.h"
#include <api/task.h>
#include <api/message.h>
#include <api/timer.h>
#include <api/link.h>
#include <api/group.h>

TASK(member, Pid boss) {
    TEST_ASSERT(GROUP_JOIN("workers") == 0);
    SEND(boss, NULL, 0, MAKE_STR_TAG("joined"));
    while (1) {
        Message *msg = RECEIVE(NO_TAG);
        if (MSG_IS(msg, MAKE_STR_TAG("job"))) {
            SEND(boss, msg->payload, msg->size, MAKE_STR_TAG("ack"));
        } else if (MSG_IS(msg, MAKE_STR_TAG("leave"))) {
            TEST_ASSERT(GROUP_LEAVE("workers") == 0);
            TEST_ASSERT(GROUP_LEAVE("workers") == -1);
            SEND(boss, NULL, 0, MAKE_STR_TAG("left"));
        }
        MSG_FREE(msg);
    }
}

static size_t members_after_change(Process *self, const char *group, size_t expected) {
    Pid pids[8];
    size_t count = GROUP_MEMBERS(group, pids, 8);
    for (int i = 0; i < 100 && count != expected; ++i) {
        SLEEP(5);
        count = GROUP_MEMBERS(group, pids, 8);
    }
    return count;
}

TASK(entry, int argc, char *argv[]) {
    (void)argc;
    (void)argv;

    Pid members[3];
    for (int i = 0; i < 3; ++i) {
        members[i] = SPAWN(member, 1, self->pid);
        MSG_FREE(RECEIVE(MAKE_STR_TAG("joined")));
    }

    // Members in join order
    Pid pids[8];
    TEST_ASSERT(GROUP_MEMBERS("workers", pids, 8) == 3);
    for (int i = 0; i < 3; ++i)
        TEST_ASSERT(pids[i] == members[i]);
    TEST_ASSERT(GROUP_MEMBERS("workers", pids, 1) == 3);

    // One send reaches every member
    int job = 7;
    TEST_ASSERT(GROUP_SEND("workers", &job, sizeof job, MAKE_STR_TAG("job")) == 3);
    for (int i = 0; i < 3; ++i) {
        Message *ack = RECEIVE(MAKE_STR_TAG("ack"));
        TEST_ASSERT(*(int *)ack->payload == 7);
        MSG_FREE(ack);
    }

    // Joining twice is joining once; a process may be in several groups
    TEST_ASSERT(GROUP_JOIN("observers") == 0 && GROUP_JOIN("observers") == 0);
    TEST_ASSERT(GROUP_JOIN("workers") == 0);
    TEST_ASSERT(GROUP_MEMBERS("observers", pids, 8) == 1 && pids[0] == self->pid);
    TEST_ASSERT(GROUP_MEMBERS("workers", pids, 8) == 4);
    TEST_ASSERT(GROUP_LEAVE("workers") == 0);

    // Leaving, and dying, remove a member
    SEND(members[0], NULL, 0, MAKE_STR_TAG("leave"));
    MSG_FREE(RECEIVE(MAKE_STR_TAG("left")));
    TEST_ASSERT(GROUP_MEMBERS("workers", pids, 8) == 2);
    KILL(members[1]);
    TEST_ASSERT(members_after_change(self, "workers", 1) == 1);
    TEST_ASSERT(GROUP_MEMBERS("workers", pids, 8) == 1 && pids[0] == members[2]);

    // Unknown groups are empty, bad names are refused
    TEST_ASSERT(GROUP_MEMBERS("nobody", pids, 8) == 0);
    TEST_ASSERT(GROUP_SEND("nobody", NULL, 0, MAKE_STR_TAG("job")) == 0);
    TEST_ASSERT(GROUP_JOIN("") == -1);
    TEST_ASSERT(GROUP_LEAVE("nobody") == -1);

    TEST_PASS();
}

CLPF_TEST_MAIN(entry)
