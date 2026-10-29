#include "test_helpers.h"
#include <api/task.h>
#include <api/message.h>
#include <string.h>

typedef struct {
    double ratio;
    char label[16];
    Pid report_to;
} Params;

// Structs and floating point arguments travel as a copied block
TASK_WITH(worker, Params, params) {
    int ok = params->ratio == 0.75 && strcmp(params->label, "alpha") == 0;
    SEND(params->report_to, &ok, sizeof ok, MAKE_STR_TAG("checked"));
    EXIT_NORMAL();
}

TASK(entry, int argc, char *argv[]) {
    (void)argc;
    (void)argv;

    Params params = {0.75, "alpha", self->pid};
    TEST_ASSERT(SPAWN_WITH(worker, &params, sizeof params) != INVALID_PID);

    // The child got its own copy
    params.ratio = 0;
    strcpy(params.label, "changed");

    Message *msg = RECEIVE(MAKE_STR_TAG("checked"));
    TEST_ASSERT(*(int *)msg->payload == 1);
    MSG_FREE(msg);

    TEST_PASS();
    EXIT_NORMAL();
}

CLPF_TEST_MAIN(entry)
