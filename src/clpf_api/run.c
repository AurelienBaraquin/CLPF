#include <api/run.h>
#include <assert.h>

void yield(Process *self) {
    assert(self);
    assert(self->scheduler_pid != -1);
    assert(clpf.scheduler_table[self->scheduler_pid].pid == self->scheduler_pid);
    assert(clpf.scheduler_table[self->scheduler_pid].scheduler_coro);
    co_switch(clpf.scheduler_table[self->scheduler_pid].scheduler_coro);
}
