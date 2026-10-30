#include "test_helpers.h"
#include <api/task.h>
#include <api/message.h>
#include <api/timer.h>
#include <api/link.h>
#include <api/bin.h>
#include <stdint.h>
#include <string.h>

#define BIN_BYTES  ((size_t)1024 * 1024)
#define FORWARDERS 8
#define HOPS       200

// Answers with where the payload is and whether it is intact, then frees it
TASK(reader, Pid boss) {
    while (1) {
        Message *msg = RECEIVE(MAKE_STR_TAG("bin"));
        uintptr_t seen[2] = {(uintptr_t)msg->payload, msg->size};
        const unsigned char *bytes = msg->payload;
        bool intact = msg->size == BIN_BYTES && bytes[0] == 0xAB && bytes[BIN_BYTES - 1] == 0xAB;
        SEND(boss, seen, sizeof seen, intact ? MAKE_STR_TAG("intact") : MAKE_STR_TAG("broken"));
        MSG_FREE(msg);
    }
}

// Keeps what it gets, in its mailbox or taken out but never freed
TASK0(hoarder) {
    MSG_FREE(RECEIVE(MAKE_STR_TAG("never")));
}

TASK0(taker) {
    RECEIVE(MAKE_STR_TAG("bin")); // never freed: released when the process ends
    MSG_FREE(RECEIVE(MAKE_STR_TAG("never")));
}

// Where a forwarder sends the binary it gets next; the binary itself is read-only
typedef struct {
    Pid to;
    bool last;
} Hop;

// Passes a binary along a ring of processes
TASK(forwarder, Pid boss) {
    while (1) {
        Message *msg = RECEIVE(MAKE_STR_TAG("hop"));
        Message *next = RECEIVE(MAKE_STR_TAG("next"));
        Hop hop = *(Hop *)next->payload;
        MSG_FREE(next);
        if (hop.last)
            SEND(boss, NULL, 0, MAKE_STR_TAG("ring_done"));
        else
            TEST_ASSERT(SEND_BIN(hop.to, msg->payload, MAKE_STR_TAG("hop")) == 0);
        MSG_FREE(msg);
    }
}

static size_t refs_after_change(Process *self, void *bin, size_t expected) {
    size_t refs = BIN_REFS(bin);
    for (int i = 0; i < 200 && refs != expected; ++i) {
        SLEEP(5);
        refs = BIN_REFS(bin);
    }
    return refs;
}

TASK(entry, int argc, char *argv[]) {
    (void)argc;
    (void)argv;

    unsigned char *bin = BIN_ALLOC(BIN_BYTES);
    TEST_ASSERT(bin && BIN_SIZE(bin) == BIN_BYTES && BIN_REFS(bin) == 1);
    if (!bin)
        return;
    memset(bin, 0xAB, BIN_BYTES);

    // The receiver reads the very same bytes: no copy
    Pid pid = SPAWN(reader, 1, self->pid);
    TEST_ASSERT(SEND_BIN(pid, bin, MAKE_STR_TAG("bin")) == 0);
    Message *answer = RECEIVE(MAKE_STR_TAG("intact"));
    TEST_ASSERT(((uintptr_t *)answer->payload)[0] == (uintptr_t)bin);
    TEST_ASSERT(((uintptr_t *)answer->payload)[1] == BIN_BYTES);
    MSG_FREE(answer);
    TEST_ASSERT(refs_after_change(self, bin, 1) == 1);

    // Ending releases what a process holds: queued, or received and never freed
    Pid hoard = SPAWN0(hoarder);
    Pid take = SPAWN0(taker);
    TEST_ASSERT(SEND_BIN(hoard, bin, MAKE_STR_TAG("bin")) == 0);
    TEST_ASSERT(SEND_BIN(take, bin, MAKE_STR_TAG("bin")) == 0);
    TEST_ASSERT(BIN_REFS(bin) == 3);
    KILL(hoard);
    KILL(take);
    TEST_ASSERT(refs_after_change(self, bin, 1) == 1);

    // A ring of processes passes it around, each hop handing its reference on
    Pid ring[FORWARDERS];
    for (int i = 0; i < FORWARDERS; ++i)
        ring[i] = SPAWN(forwarder, 1, self->pid);
    for (int i = 0; i < HOPS; ++i) {
        Hop hop = {.to = ring[(i + 1) % FORWARDERS], .last = i == HOPS - 1};
        SEND(ring[i % FORWARDERS], &hop, sizeof hop, MAKE_STR_TAG("next"));
    }
    TEST_ASSERT(SEND_BIN(ring[0], bin, MAKE_STR_TAG("hop")) == 0);
    Message *done = RECEIVE_TIMEOUT(MAKE_STR_TAG("ring_done"), 5000);
    TEST_ASSERT(done);
    MSG_FREE(done);
    TEST_ASSERT(refs_after_change(self, bin, 1) == 1);

    // Errors: no such process, not a binary, freeing what one does not hold
    KILL(pid);
    SLEEP(10);
    TEST_ASSERT(SEND_BIN(pid, bin, MAKE_STR_TAG("bin")) == -1);
    TEST_ASSERT(BIN_REFS(bin) == 1);
    static char plain[256];
    TEST_ASSERT(SEND_BIN(self->pid, plain + 128, MAKE_STR_TAG("bin")) == -1);
    TEST_ASSERT(BIN_FREE(plain + 128) == -1);
    TEST_ASSERT(BIN_FREE(bin) == 0);

    TEST_PASS();
}

CLPF_TEST_MAIN(entry)
