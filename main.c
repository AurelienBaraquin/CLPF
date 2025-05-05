#include <clpf_internal.h>
#include <stdio.h>

#define SPAWN(func, argc, ...) \
    do { \
        Funcall_t funcall; \
        funcall_init(&funcall, func, argc, __VA_ARGS__); \
        __clpf_request_task_add(&funcall, PROCESS_PRIORITY_MEDIUM, self->pid); \
    } while (0)

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
