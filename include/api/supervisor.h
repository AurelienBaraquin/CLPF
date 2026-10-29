#pragma once

#include <native_task/supervisor.h>
#include <api/task.h>
#include <api/message.h>
#include <api/tag.h>

/**
 * @brief Macro to spawn a supervisor process with specified parameters.
 *
 * This macro is used to create a supervisor process with a given strategy,
 * monitoring period, and maximum restart attempts. It internally calls the
 * SPAWN macro with the supervisor-specific parameters.
 * Default values are set when 0 is passed for period and max_restart.
 *
 * @param strat The supervision strategy (only SUP_ONE_FOR_ONE is implemented, others make the supervisor exit).
 * @param period The monitoring period in milliseconds. (default: 1000ms)
 * @param max_restart The maximum number of restarts of a child within period,
 *        below SUP_RESTART_HISTORY. (default: 5)
 */
#define SPAWN_SUPERVISOR(name, strat, period, max_restart) \
    SPAWN_NAMED(__supervisor__, name, 3, strat, period, max_restart)

// Returns the pid of the started child, or INVALID_PID
Pid __sup_add_child(Process *self, char *name, Pid sup, RestartType restart, process_priority prio, void (*func)(), int argc, ...);
// Stops supervising a child (the child keeps running), returns -1 if sup does not supervise it
int __sup_remove_child(Process *self, Pid sup, Pid child);

#define SUP_ADD_CHILD_PRIO(sup, restart, func, name, prio, argc, ...) \
    __sup_add_child(self, name, sup, restart, prio, func, argc, __VA_ARGS__)

#define SUP_ADD_CHILD_PRIO0(sup, restart, func, name, prio) \
    __sup_add_child(self, name, sup, restart, prio, func, 0)

#define SUP_ADD_CHILD(sup, restart, func, name, argc, ...) \
    __sup_add_child(self, name, sup, restart, PROCESS_PRIORITY_MEDIUM, func, argc, __VA_ARGS__)

#define SUP_ADD_CHILD0(sup, restart, func, name) \
    __sup_add_child(self, name, sup, restart, PROCESS_PRIORITY_MEDIUM, func, 0)

#define SUP_REMOVE_CHILD(sup, pid) \
    __sup_remove_child(self, sup, pid)
