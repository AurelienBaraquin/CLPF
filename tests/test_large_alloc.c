#include "test_helpers.h"
#include <api/task.h>
#include <api/message.h>
#include <api/memory.h>
#include <string.h>

#define LARGE ((size_t)1024 * 1024)

TASK(receiver, Pid boss) {
    Message *msg = RECEIVE(MAKE_ATOM_TAG("large"));
    const unsigned char *bytes = msg->payload;
    bool intact = msg->size == LARGE && bytes[0] == 7 && bytes[LARGE - 1] == 7;
    MSG_FREE(msg);
    SEND(boss, &intact, sizeof intact, MAKE_ATOM_TAG("checked"));
}

TASK(entry, int argc, char *argv[]) {
    (void)argc;
    (void)argv;

    // Larger than the free space of the heap: it grows enough for it
    unsigned char *block = ALLOC(LARGE);
    TEST_ASSERT(block != NULL);
    if (!block)
        return;
    memset(block, 7, LARGE);

    // A large message grows the receiver's heap the same way
    TEST_ASSERT(SEND(SPAWN(receiver, 1, self->pid), block, LARGE, MAKE_ATOM_TAG("large")) == 0);
    Message *checked = RECEIVE(MAKE_ATOM_TAG("checked"));
    TEST_ASSERT(*(bool *)checked->payload);
    MSG_FREE(checked);
    FREE(block);

    TEST_PASS();
}

CLPF_TEST_MAIN(entry)
