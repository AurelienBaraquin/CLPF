#include "test_helpers.h"
#include <api/supervisor.h>
#include <string.h>

typedef struct {
    double rate;
    char label[16];
    Pid report_to;
} Config;

static atomic_bool crashed;

static int recurse(int depth) {
    volatile char frame[256];
    frame[0] = (char)depth;
    return depth == 0 ? frame[0] : recurse(depth - 1) + frame[0];
}

// Needs a big stack and its config block, crashes once: the restart must get both again
TASK_WITH(configured, Config, config) {
    recurse(1000); // about 300 KiB of stack
    int ok = config->rate == 0.5 && strcmp(config->label, "alpha") == 0;
    SEND(config->report_to, &ok, sizeof ok, MAKE_STR_TAG("started"));
    if (!atomic_exchange(&crashed, true))
        EXIT_ERROR();
    MSG_FREE(RECEIVE(MAKE_STR_TAG("never")));
}

TASK(entry, int argc, char *argv[]) {
    (void)argc;
    (void)argv;

    Pid sup = SPAWN_SUPERVISOR("sup", SUP_ONE_FOR_ONE, 1000, 3);
    Config config = {0.5, "alpha", self->pid};
    Pid child = SUP_ADD_CHILD_OPTS(sup, RES_PERMANENT, configured, .name = "configured", .arg_block = &config,
                                   .arg_block_size = sizeof config, .stack_size = (size_t)1024 * 1024);
    TEST_ASSERT(child != INVALID_PID);
    memset(&config, 0, sizeof config); // the supervisor keeps its own copy

    for (int i = 0; i < 2; ++i) { // the first start and the restart
        Message *msg = RECEIVE(MAKE_STR_TAG("started"));
        TEST_ASSERT(*(int *)msg->payload == 1);
        MSG_FREE(msg);
    }

    TEST_PASS();
}

CLPF_TEST_MAIN(entry)
