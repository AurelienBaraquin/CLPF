#include <api/run.h>

void yield(Process *self) {
    co_switch(clpf.scheduler_table[self->scheduler_pid].scheduler_coro);
}
