#include <api/run.h>
#include <assert.h>

void yield(Process *self) {
    assert(self);
    assert(self->scheduler_pid != -1);
    // printf("Process %ld yield\n", self->pid);
    // printf("Scheduler %ld yield\n", self->scheduler_pid);
    printf("Process %ld yield in scheduler %ld\n", self->pid, self->scheduler_pid);
    assert(clpf.scheduler_table[self->scheduler_pid].scheduler_coro);
    co_switch(clpf.scheduler_table[self->scheduler_pid].scheduler_coro);
}
