#pragma once

#include <clpf_internal.h>

#define TASK0(_name) \
    void _name(Process *self) \

#define TASK(_name, ...) \
    void _name(Process *self, __VA_ARGS__) \

// A task receiving a copied argument block (see SPAWN_WITH)
#define TASK_WITH(_name, type, var) \
    void _name(Process *self, type *var) \

#define DEF(_name, ...) \
    _name(Process *self, __VA_ARGS__) \

#define CALL(_name, ...) \
    _name(self, __VA_ARGS__)

#define CLPF_ENTRY(task) \
    int main(int argc, char *argv[]) { \
        clpf_main(task, argc, argv); \
        return 0; \
    }

typedef enum exit_reason_e {
    EXIT_REASON_NORMAL,
    EXIT_REASON_ERROR,
    EXIT_REASON_NOPROC, // The monitored process did not exist (anymore) when the monitor was set
    EXIT_REASON_KILLED, // Ended by KILL
} exit_reason;

#define EXIT(reason) \
    exiting(self, reason)

#define EXIT_NORMAL() \
    exiting(self, EXIT_REASON_NORMAL)
#define EXIT_ERROR() \
    exiting(self, EXIT_REASON_ERROR)

#define PARENT() \
    process_get_parent_pid(self)

#define CHILD(index) \
    process_get_child_pid(self, index)

#define NB_CHILDREN() \
    process_get_child_count(self)

#define YIELD() \
    yield(self);
