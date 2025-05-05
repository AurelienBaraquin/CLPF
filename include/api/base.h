#pragma once

#include <clpf_internal.h>

#define TASK(name, ...) \
    void name(Process *self, __VA_ARGS__) \

#define CLPF_ENTRY(task) \
    int main(int argc, char *argv[]) { \
        __clpf_main(task, argc, argv); \
        return 0; \
    }

#define EXIT() \
    do { \
        self->state = PROCESS_EXITING; \
        return; \
    } while (0)

#define PARENT() \
    process_get_parent_pid(self)
