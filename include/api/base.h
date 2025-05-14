#pragma once

#include <clpf_internal.h>

#define TASK0(_name) \
    void _name(Process *self) \

#define TASK(_name, ...) \
    void _name(Process *self, __VA_ARGS__) \

#define DEF(name, ...) \
    name(Process *self, __VA_ARGS__) \

#define CALL(name, ...) \
    name(self, __VA_ARGS__)

#define CLPF_ENTRY(task) \
    int main(int argc, char *argv[]) { \
        __clpf_main(task, argc, argv); \
        return 0; \
    }

#define EXIT() \
    exiting(self); \

#define PARENT() \
    process_get_parent_pid(self)

#define CHILD(index) \
    process_get_child_pid(self, index)

#define NB_CHILDREN() \
    process_get_child_count(self)

#define YIELD() \
    yield(self);
