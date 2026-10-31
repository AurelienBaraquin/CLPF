#include "test_helpers.h"
#include <api/task.h>
#include <api/message.h>
#include <api/timer.h>
#include <utils/time.h>

#ifdef CLPF_SANITIZED
#define QUICK_TIMEOUTS 5000
#else
#define QUICK_TIMEOUTS 20000
#endif
#define MAX_QUICK_MS 1000
#define PENDING      50000
#define PINNED       16

TASK0(echo) {
    while (1) {
        Message *msg = RECEIVE(NO_TAG);
        SEND(msg->sender_pid, NULL, 0, MAKE_ATOM_TAG("pong"));
        MSG_FREE(msg);
    }
}

// Arms a timer, hands it to the boss to cancel, and checks it never fires
TASK(armer, Pid boss) {
    TimerId id = SEND_AFTER(self->pid, 100, NULL, 0, MAKE_ATOM_TAG("too_late"));
    SEND(boss, &id, sizeof id, MAKE_ATOM_TAG("armed"));
    MSG_FREE(RECEIVE(MAKE_ATOM_TAG("cancelled")));
    TEST_ASSERT(RECEIVE_TIMEOUT(MAKE_ATOM_TAG("too_late"), 200) == NULL);
    SEND(boss, NULL, 0, MAKE_ATOM_TAG("checked"));
}

// A timer on a chosen scheduler
TASK(ticker, Pid boss) {
    SEND_AFTER(self->pid, 10, NULL, 0, MAKE_ATOM_TAG("tick"));
    Message *tick = RECEIVE_TIMEOUT(MAKE_ATOM_TAG("tick"), 1000);
    TEST_ASSERT(tick);
    MSG_FREE(tick);
    SEND(boss, NULL, 0, MAKE_ATOM_TAG("ticked"));
}

// Ends before its timer fires
TASK(short_lived, Pid boss) {
    SEND_AFTER(boss, 30, NULL, 0, MAKE_ATOM_TAG("posthumous"));
}

TASK(entry, int argc, char *argv[]) {
    (void)argc;
    (void)argv;

    // A RECEIVE_TIMEOUT that waits for an answer arms and cancels a timer: both
    // stay cheap however many other timers are pending
    static TimerId pending[PENDING];
    for (int i = 0; i < PENDING; ++i)
        pending[i] = SEND_AFTER(self->pid, 30000, NULL, 0, MAKE_ATOM_TAG("later"));
    Pid partner = SPAWN0(echo);
    uint64_t start = now_ms();
    for (int i = 0; i < QUICK_TIMEOUTS; ++i) {
        SEND(partner, NULL, 0, MAKE_ATOM_TAG("ping"));
        Message *msg = RECEIVE_TIMEOUT(MAKE_ATOM_TAG("pong"), 60000);
        TEST_ASSERT(msg);
        MSG_FREE(msg);
    }
    TEST_ASSERT(now_ms() - start < MAX_QUICK_MS);
    for (int i = 0; i < PENDING; ++i)
        TEST_ASSERT(CANCEL_TIMER(pending[i]) == 0);

    // Any process may cancel a timer, once
    Pid pid = SPAWN(armer, 1, self->pid);
    Message *armed = RECEIVE(MAKE_ATOM_TAG("armed"));
    TimerId id = *(TimerId *)armed->payload;
    MSG_FREE(armed);
    TEST_ASSERT(CANCEL_TIMER(id) == 0);
    TEST_ASSERT(CANCEL_TIMER(id) == -1);
    SEND(pid, NULL, 0, MAKE_ATOM_TAG("cancelled"));
    MSG_FREE(RECEIVE(MAKE_ATOM_TAG("checked")));

    // Ids that never were, or are spent, are refused
    TEST_ASSERT(CANCEL_TIMER(0) == -1);
    TEST_ASSERT(CANCEL_TIMER((TimerId)-1) == -1);
    TimerId fired = SEND_AFTER(self->pid, 1, NULL, 0, MAKE_ATOM_TAG("fired"));
    MSG_FREE(RECEIVE(MAKE_ATOM_TAG("fired")));
    TEST_ASSERT(CANCEL_TIMER(fired) == -1);

    // Timers fire on every scheduler
    for (int i = 0; i < PINNED; ++i) {
        Funcall_t call;
        funcall_init(&call, ticker, 1, self->pid);
        clpf_spawn(self, &call, (SpawnOptions){.scheduler = 1 + i % clpf.scheduler_count});
    }
    for (int i = 0; i < PINNED; ++i) {
        Message *ticked = RECEIVE_TIMEOUT(MAKE_ATOM_TAG("ticked"), 2000);
        TEST_ASSERT(ticked);
        MSG_FREE(ticked);
    }

    // A timer outlives the process that armed it
    SPAWN(short_lived, 1, self->pid);
    Message *posthumous = RECEIVE_TIMEOUT(MAKE_ATOM_TAG("posthumous"), 1000);
    TEST_ASSERT(posthumous);
    MSG_FREE(posthumous);

    TEST_PASS();
}

CLPF_TEST_MAIN(entry)
