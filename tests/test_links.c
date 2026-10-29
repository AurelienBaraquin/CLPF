#include "test_helpers.h"
#include <api/task.h>
#include <api/message.h>
#include <api/monitor.h>
#include <api/link.h>
#include <api/timer.h>

static unsigned int down_reason(Process *self, Pid pid) {
    Message *msg = RECEIVE(MAKE_DOWN_TAG(pid));
    unsigned int reason = ((DownInfo *)msg->payload)->reason;
    MSG_FREE(msg);
    return reason;
}

// Answers pings until told otherwise; dies with "error" or "normal" on request
TASK0(server) {
    while (1) {
        Message *msg = RECEIVE(NO_TAG);
        if (MSG_IS(msg, MAKE_STR_TAG("ping"))) {
            SEND(msg->sender_pid, NULL, 0, MAKE_STR_TAG("pong"));
        } else if (MSG_IS(msg, MAKE_STR_TAG("crash"))) {
            EXIT_ERROR();
        } else if (MSG_IS(msg, MAKE_STR_TAG("stop"))) {
            EXIT_NORMAL();
        } else if (MSG_IS(msg, MAKE_STR_TAG("trap"))) {
            TRAP_EXIT(true);
            SEND(msg->sender_pid, NULL, 0, MAKE_STR_TAG("trapping"));
        } else if (MSG_IS(msg, MAKE_STR_TAG("link"))) {
            LINK(*(Pid *)msg->payload);
            SEND(msg->sender_pid, NULL, 0, MAKE_STR_TAG("linked"));
        } else if (msg->tag.kind == TAG_KIND_EXIT) {
            // Show trapped exits to the test
            SEND(PARENT(), msg->payload, msg->size, MAKE_STR_TAG("trapped"));
        }
        MSG_FREE(msg);
    }
}

TASK0(busy) {
    while (1)
        YIELD();
}

TASK0(kill_myself) {
    KILL(self->pid);
    EXIT_NORMAL(); // never reached
}

TASK0(crash_now) {
    EXIT_ERROR();
}

// Spawns a linked child that crashes at once: the link takes this process down
TASK0(linked_parent) {
    SPAWN_LINK(crash_now, 0);
    MSG_FREE(RECEIVE(MAKE_STR_TAG("never")));
}

static bool alive(Process *self, Pid pid) {
    if (SEND(pid, NULL, 0, MAKE_STR_TAG("ping")) != 0)
        return false;
    Message *pong = RECEIVE_TIMEOUT(MAKE_STR_TAG("pong"), 1000);
    MSG_FREE(pong);
    return pong != NULL;
}

static void link_them(Process *self, Pid a, Pid b) {
    SEND(a, &b, sizeof b, MAKE_STR_TAG("link"));
    MSG_FREE(RECEIVE(MAKE_STR_TAG("linked")));
}

TASK(entry, int argc, char *argv[]) {
    (void)argc;
    (void)argv;

    // KILL ends a waiting process and a running one
    Pid waiting = SPAWN0(server);
    MONITOR(waiting);
    TEST_ASSERT(KILL(waiting) == 0);
    TEST_ASSERT(down_reason(self, waiting) == EXIT_REASON_KILLED);
    TEST_ASSERT(KILL(waiting) == -1);

    Pid running = SPAWN0(busy);
    MONITOR(running);
    SLEEP(10);
    KILL(running);
    TEST_ASSERT(down_reason(self, running) == EXIT_REASON_KILLED);

    Pid suicidal = SPAWN0(kill_myself);
    MONITOR(suicidal);
    TEST_ASSERT(down_reason(self, suicidal) == EXIT_REASON_KILLED);

    // An abnormal exit travels along a link...
    Pid a = SPAWN0(server), b = SPAWN0(server);
    MONITOR(a);
    link_them(self, a, b);
    SEND(b, NULL, 0, MAKE_STR_TAG("crash"));
    TEST_ASSERT(down_reason(self, a) == EXIT_REASON_ERROR);

    // ...a normal one does not
    a = SPAWN0(server);
    b = SPAWN0(server);
    link_them(self, a, b);
    SEND(b, NULL, 0, MAKE_STR_TAG("stop"));
    SLEEP(20);
    TEST_ASSERT(alive(self, a));

    // UNLINK cuts the link: b crashing does not take this process down
    b = SPAWN0(server);
    MONITOR(b);
    TEST_ASSERT(LINK(b) == 0);
    TEST_ASSERT(UNLINK(b) == 0);
    TEST_ASSERT(UNLINK(b) == -1);
    SEND(b, NULL, 0, MAKE_STR_TAG("crash"));
    TEST_ASSERT(down_reason(self, b) == EXIT_REASON_ERROR);
    TEST_ASSERT(LINK(b) == -1); // gone

    // A process trapping exits gets them as messages and survives
    Pid trapper = SPAWN0(server);
    SEND(trapper, NULL, 0, MAKE_STR_TAG("trap"));
    MSG_FREE(RECEIVE(MAKE_STR_TAG("trapping")));
    Pid victim = SPAWN0(server);
    link_them(self, trapper, victim);
    SEND(victim, NULL, 0, MAKE_STR_TAG("crash"));
    Message *trapped = RECEIVE(MAKE_STR_TAG("trapped"));
    ExitInfo *exit_info = trapped->payload;
    TEST_ASSERT(exit_info->from == victim && exit_info->reason == EXIT_REASON_ERROR);
    MSG_FREE(trapped);
    TEST_ASSERT(alive(self, trapper));

    // SEND_EXIT: ignored when normal, trapped when trapping, fatal otherwise
    Pid target = SPAWN0(server);
    MONITOR(target);
    SEND_EXIT(target, EXIT_REASON_NORMAL);
    TEST_ASSERT(alive(self, target));
    SEND_EXIT(target, EXIT_REASON_ERROR);
    TEST_ASSERT(down_reason(self, target) == EXIT_REASON_ERROR);

    // KILL cannot be trapped
    MONITOR(trapper);
    KILL(trapper);
    TEST_ASSERT(down_reason(self, trapper) == EXIT_REASON_KILLED);

    // SPAWN_LINK links before the child can run
    Pid parent = SPAWN0(linked_parent);
    MONITOR(parent);
    TEST_ASSERT(down_reason(self, parent) == EXIT_REASON_ERROR);

    KILL(a);
    TEST_PASS();
    EXIT_NORMAL();
}

CLPF_TEST_MAIN(entry)
