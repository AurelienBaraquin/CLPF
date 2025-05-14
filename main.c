#include <api/task.h>
#include <api/message.h>
#include <api/base.h>
#include <api/tag.h>
#include <api/memory.h>
#include <api/io.h>
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

void on_signal_down(Process *self, Pid from, unsigned int reason) {
    PRINT("Process %ld received signal down from %ld with reason %u\n", self->pid, from, reason);
}

TASK(task, int argc, char *argv[]) {
    int count = 0;
    Pid children[100];

    char *r = GETLINE();
    assert(r);

    count = atoi(r);

    FREE(r);

    self->monitor.on_process_down = &on_signal_down;

    for (int i = 0; i < count; ++i) {
        children[i] = SPAWN0(child);
        monitor_add(&self->monitor, children[i]);
        monitored_by_add(&clpf.process_table[children[i]].monitor, self->pid);
    }

    for (int i = 0; i < count; ++i) {
        SEND(children[i], &i, sizeof(int), NO_TAG);
    }

    YIELD();

    exit_in_progress = true;

    EXIT();
}

CLPF_ENTRY(task)
