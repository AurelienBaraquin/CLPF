#include "test_helpers.h"
#include <api/task.h>
#include <api/message.h>
#include <stdio.h>
#include <string.h>

#define NAMERS 4
#define NAMES  200

// Interns the same dynamic names as the others, and reports what it got
TASK(namer, Pid boss) {
    static const char *seen[NAMERS][NAMES];
    static _Atomic int next_row;
    int row = atomic_fetch_add(&next_row, 1);
    for (int i = 0; i < NAMES; ++i) {
        char name[32];
        snprintf(name, sizeof name, "dynamic:%d", i);
        seen[row][i] = clpf_atom(name);
        REDUCE(1);
    }
    SEND(boss, &seen[row], sizeof seen[row], MAKE_ATOM_TAG("named"));
}

TASK(entry, int argc, char *argv[]) {
    (void)argc;
    (void)argv;

    // One atom per name: the same pointer at every call site, for any equal string
    Tag first = MAKE_ATOM_TAG("ping");
    Tag second = MAKE_ATOM_TAG("ping");
    char built[8];
    snprintf(built, sizeof built, "pi%s", "ng");
    TEST_ASSERT(first.kind == TAG_KIND_ATOM && first.str_value == second.str_value);
    TEST_ASSERT(clpf_atom_tag(built).str_value == first.str_value);
    TEST_ASSERT(MAKE_ATOM_TAG("pong").str_value != first.str_value);

    // Sent without copying the name, matched by pointer
    SEND(self->pid, NULL, 0, MAKE_ATOM_TAG("ping"));
    Message *msg = RECEIVE(MAKE_ATOM_TAG("ping"));
    TEST_ASSERT(msg && msg->tag.str_value == first.str_value);
    MSG_FREE(msg);

    // An atom and a string tag with the same text are different tags
    SEND(self->pid, NULL, 0, MAKE_STR_TAG("ping"));
    TEST_ASSERT(FETCH(MAKE_ATOM_TAG("ping")) == NULL);
    MSG_FREE(FETCH(MAKE_STR_TAG("ping")));

    // TM_PREFIX works on atoms and strings alike
    SEND(self->pid, NULL, 0, MAKE_ATOM_TAG("job::resize"));
    SEND(self->pid, NULL, 0, MAKE_STR_TAG("job::crop"));
    Message *atom_job = FETCH_IF(TM_PREFIX, MAKE_ATOM_TAG("job::"));
    Message *string_job = FETCH_IF(TM_PREFIX, MAKE_STR_TAG("job::"));
    TEST_ASSERT(atom_job && atom_job->tag.kind == TAG_KIND_ATOM);
    TEST_ASSERT(string_job && string_job->tag.kind == TAG_KIND_STRING);
    MSG_FREE(atom_job);
    MSG_FREE(string_job);

    // Concurrent interning agrees
    for (int i = 0; i < NAMERS; ++i)
        SPAWN(namer, 1, self->pid);
    const char *reference[NAMES];
    for (int i = 0; i < NAMERS; ++i) {
        Message *named = RECEIVE(MAKE_ATOM_TAG("named"));
        const char *const *names = (const char *const *)named->payload;
        for (int n = 0; n < NAMES; ++n) {
            if (i == 0)
                reference[n] = names[n];
            TEST_ASSERT(names[n] == reference[n]);
        }
        MSG_FREE(named);
    }
    TEST_ASSERT(strcmp(reference[7], "dynamic:7") == 0);

    TEST_PASS();
}

CLPF_TEST_MAIN(entry)
