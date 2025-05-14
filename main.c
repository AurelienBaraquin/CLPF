#include <api/task.h>
#include <api/message.h>
#include <api/base.h>
#include <api/tag.h>
#include <api/memory.h>
#include <api/io.h>
#include <api/monitor.h>
#include <assert.h>
#include <msignal.h>

#include <flags_internal.h>

int DEF(add, int a, int b) {
    return a + b;
}

TASK0(child) {
    int *msg = RECEIVE(NO_TAG);
    if (msg) {
        PRINT("Child %d: Hello from child process %d\n", CALL(add, 10, *msg), self->pid);
        FREE(msg);
    }

    EXIT();
}

void DEF(on_signal_down, Pid from, unsigned int reason) {
    PRINT("Process %ld received signal down from %ld with reason %u\n", self->pid, from, reason);
}

TASK(task, int argc, char *argv[]) {
    MONITOR_INIT(on_signal_down);
    int count = 0;
    Pid children[100];

    char *r = GETLINE();
    assert(r);

    count = atoi(r);

    FREE(r);

    for (int i = 0; i < count; ++i) {
        children[i] = SPAWN0(child);
        MONITOR_ADD(children[i]);
    }

    for (int i = 0; i < count; ++i) {
        SEND(children[i], &i, sizeof(int), NO_TAG);
    }

    YIELD();

    exit_in_progress = true;

    EXIT();
}

CLPF_ENTRY(task)
