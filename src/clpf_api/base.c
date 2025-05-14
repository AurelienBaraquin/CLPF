#include <api/base.h>
#include <assert.h>

void yield(Process *self) {
    assert(self);
    assert(self->scheduler_pid != -1);
    assert(clpf.scheduler_table[self->scheduler_pid].pid == self->scheduler_pid);
    assert(clpf.scheduler_table[self->scheduler_pid].scheduler_acoro);
    aco_yield();

    handle_callbacks(self);
}

void exiting(Process *self) {
    for (links_t *l = self->monitor.monitored_by; l; l = l->next) {
        signal_send(self, l->pid, SIGNAL_DOWN, 43);
    }

    self->state = PROCESS_EXITING;
}
