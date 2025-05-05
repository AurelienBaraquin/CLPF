#include <clpf_internal.h>
#include <stdio.h>

void other_task(Process *self, int a) {
    printf("other task running, parent pid: %d\n", a);
}

void task(Process *self, int argc, char *argv[]) {
    Funcall_t other_task_fun;
    funcall_init(&other_task_fun, other_task, 1, 20);
    __clpf_request_task_add(&other_task_fun, PROCESS_PRIORITY_MEDIUM, self->pid);
    for (int i = 0; i < argc; i++) {
        printf("arg %d: %s\n", i, argv[i]);
    }
}

int main(int argc, char *argv[]) {
    clpf_main(task, argc, argv);
}
