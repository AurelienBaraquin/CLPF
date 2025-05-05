#include <api/task.h>
#include <stdio.h>

void yield(Process *self) {
    self->state = PROCESS_WAITING;
    co_switch(self->coro);
}

void other_task(Process *self, int a) {
    printf("other task running, parent pid: %d\n", a);
}

void task(Process *self, int argc, char *argv[]) {
    SPAWN(other_task, 1, 20);
    for (int i = 0; i < argc; i++) {
        printf("arg %d: %s\n", i, argv[i]);
    }
}

int main(int argc, char *argv[]) {
    clpf_main(task, argc, argv);
}
