#pragma once

#include <native_task/supervisor.h>
#include <api/task.h>
#include <api/message.h>
#include <api/registry.h>

/**
 * @brief Macro to spawn a supervisor process with specified parameters.
 *
 * This macro is used to create a supervisor process with a given strategy,
 * monitoring period, and maximum restart attempts. It internally calls the
 * SPAWN macro with the supervisor-specific parameters.
 * Default values are set when 0 is passed for period and max_restart.
 *
 * @param strat The supervision strategy (SUP_ONE_FOR_ONE, SUP_ONE_FOR_ALL, SUP_REST_FOR_ONE).
 * @param period The monitoring period in milliseconds. (default: 1000ms)
 * @param max_restart The maximum number of restarts, all children together, within
 *        period. One more and the supervisor kills its children and exits with
 *        EXIT_REASON_ERROR, which its monitors and links see. (default: 5)
 */
#define SPAWN_SUPERVISOR(name, strat, period, max_restart) \
    SPAWN_NAMED(clpf_supervisor, name, 3, strat, period, max_restart)

// Returns the pid of the started child, or INVALID_PID
Pid clpf_sup_add_child(Process *self, char *name, Pid sup, RestartType restart, process_priority prio, void (*func)(),
                       int argc, ...);
// Stops supervising a child (the child keeps running), returns -1 if sup does not supervise it
int clpf_sup_remove_child(Process *self, Pid sup, Pid child);

#define SUP_ADD_CHILD_PRIO(sup, restart, func, name, prio, argc, ...) \
    clpf_sup_add_child(self, name, sup, restart, prio, func, argc, __VA_ARGS__)

#define SUP_ADD_CHILD_PRIO0(sup, restart, func, name, prio) clpf_sup_add_child(self, name, sup, restart, prio, func, 0)

#define SUP_ADD_CHILD(sup, restart, func, name, argc, ...) \
    clpf_sup_add_child(self, name, sup, restart, PROCESS_PRIORITY_MEDIUM, func, argc, __VA_ARGS__)

#define SUP_ADD_CHILD0(sup, restart, func, name) \
    clpf_sup_add_child(self, name, sup, restart, PROCESS_PRIORITY_MEDIUM, func, 0)

// Adds a child with any SpawnOptions: name, priority, stack_size, mailbox_limit, and an
// argument block for a TASK_WITH task, copied by the supervisor and given to every restart.
// The supervisor decides monitoring, linking and placement itself.
#define SUP_ADD_CHILD_OPTS(sup, restart, func, ...) \
    clpf_sup_add_child_opts(self, sup, restart, func, (SpawnOptions){__VA_ARGS__})

Pid clpf_sup_add_child_opts(Process *self, Pid sup, RestartType restart, void (*func)(), SpawnOptions options);

#define SUP_REMOVE_CHILD(sup, pid) clpf_sup_remove_child(self, sup, pid)
