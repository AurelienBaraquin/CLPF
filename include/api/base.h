#pragma once

#include <clpf_internal.h>

#define TASK0(_name) void _name(Process *self)

#define TASK(_name, ...) void _name(Process *self, __VA_ARGS__)

// A task receiving a copied argument block (see SPAWN_WITH)
#define TASK_WITH(_name, type, var) void _name(Process *self, type *var)

#define DEF(_name, ...) _name(Process *self, __VA_ARGS__)

#define CALL(_name, ...) _name(self, __VA_ARGS__)

// The program ends when the entry task ends: 0 if it exited normally, 1 otherwise
#define CLPF_ENTRY(task)                    \
    int main(int argc, char *argv[]) {      \
        return clpf_main(task, argc, argv); \
    }

typedef enum exit_reason_e {
    EXIT_REASON_NORMAL,
    EXIT_REASON_ERROR,
    EXIT_REASON_NOPROC,     // The monitored process did not exist (anymore) when the monitor was set
    EXIT_REASON_KILLED,     // Ended by KILL
    EXIT_REASON_SHUTDOWN,   // Asked to stop by its supervisor or the runtime
    EXIT_REASON_HEAP_LIMIT, // Its heap would have grown past SpawnOptions.max_heap_size
} exit_reason;

#define EXIT(reason) exiting(self, reason)

#define EXIT_NORMAL() exiting(self, EXIT_REASON_NORMAL)
#define EXIT_ERROR()  exiting(self, EXIT_REASON_ERROR)

#define PARENT() process_get_parent_pid(self)

#define CHILD(index) process_get_child_pid(self, index)

#define NB_CHILDREN() process_get_child_count(self)

#define YIELD() yield(self)

// Drops the call stack, gives the stack and the unused heap back, and waits
// for a message: then the process starts again in func with these arguments
// (integers, as for SPAWN). Pid, mailbox, links, monitors and name stay.
// For processes that wait long: they then cost little more than their heap.
#define HIBERNATE(func, argc, ...)                         \
    do {                                                   \
        Funcall_t clpf_next;                               \
        funcall_init(&clpf_next, func, argc, __VA_ARGS__); \
        clpf_hibernate(self, &clpf_next);                  \
    } while (0)
#define HIBERNATE0(func)                   \
    do {                                   \
        Funcall_t clpf_next;               \
        funcall_init(&clpf_next, func, 0); \
        clpf_hibernate(self, &clpf_next);  \
    } while (0)

// Accounts for n units of work. The scheduler is cooperative: a process only
// gives the CPU away at API calls, so a long computation should REDUCE
// regularly, or it keeps its scheduler to itself.
#define REDUCE(n) clpf_reduce(self, n)
