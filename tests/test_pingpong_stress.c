#include "test_helpers.h"
#include <api/task.h>
#include <api/message.h>
#include <api/memory.h>

#define PAIRS 8
#define ROUND_TRIPS 2000

// Root processes are spread over the schedulers, so every message below
// crosses threads. A lost wakeup leaves a process waiting forever and the
// test hits its timeout.
static Pid spawn_root(Process *self, Funcall_t *taskcall) {
    __clpf_request_task_add(self, "", true, false, taskcall, PROCESS_PRIORITY_MEDIUM, INVALID_PID);
    Pid *pid = RECEIVE(CORE_MESSAGE_TYPE_SPAWN_TAG);
    Pid result = *pid;
    FREE(pid);
    return result;
}

TASK0(echo) {
    for (int i = 0; i < ROUND_TRIPS; ++i) {
        Message *msg = RECEIVE_RAW(MAKE_STR_TAG("ping"));
        SEND(msg->sender_pid, msg->payload, msg->size, MAKE_STR_TAG("pong"));
        FREE(msg->payload);
        FREE(msg);
    }
    EXIT_NORMAL();
}

TASK(pinger, Pid echo_pid, Pid report_to) {
    int ok = 1;
    for (int i = 0; i < ROUND_TRIPS; ++i) {
        SEND(echo_pid, &i, sizeof i, MAKE_STR_TAG("ping"));
        int *r = RECEIVE(MAKE_STR_TAG("pong"));
        ok &= *r == i;
        FREE(r);
    }
    SEND(report_to, &ok, sizeof ok, MAKE_STR_TAG("done"));
    EXIT_NORMAL();
}

TASK(entry, int argc, char *argv[]) {
    (void)argc;
    (void)argv;

    for (int i = 0; i < PAIRS; ++i) {
        Funcall_t echo_call;
        funcall_init(&echo_call, echo, 0);
        Pid echo_pid = spawn_root(self, &echo_call);
        TEST_ASSERT(echo_pid != INVALID_PID);

        Funcall_t pinger_call;
        funcall_init(&pinger_call, pinger, 2, echo_pid, self->pid);
        TEST_ASSERT(spawn_root(self, &pinger_call) != INVALID_PID);
    }

    for (int i = 0; i < PAIRS; ++i) {
        int *ok = RECEIVE(MAKE_STR_TAG("done"));
        TEST_ASSERT(*ok == 1);
        FREE(ok);
    }

    TEST_PASS();
    EXIT_NORMAL();
}

CLPF_TEST_MAIN(entry)
