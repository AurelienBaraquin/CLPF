#include <clpf_internal.h>
#include <stdio.h>

void other_task(Process *self, int a) {
    printf("other task running, parent pid: %d\n", a);
}

void task(Process *self, int argc, char *argv[]) {
    for (int i = 0; i < argc; i++) {
        printf("arg %d: %s\n", i, argv[i]);
    }
}

int main(int argc, char *argv[]) {
    clpf_main(task, argc, argv);
}
