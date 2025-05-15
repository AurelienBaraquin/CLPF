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
 * @param strat The supervision strategy to be used (e.g., restart strategy).
 * @param period The monitoring period in milliseconds. (default: 1000ms)
 * @param max_restart The maximum number of restart attempts allowed. (default: 5)
 */
#define SPAWN_SUPERVISOR(strat, period, max_restart) \
    SPAWN(__supervisor__, 3, strat, period, max_restart); \

Pid __sup_add_child(Process *self, char *name, Pid sup, SUP_STRAT strat, void (*func)(), int argc, ...);

#define SUP_ADD_CHILD(sup, name, strat, func, argc, ...) \
    __sup_add_child(self, name, sup, strat, func, argc, __VA_ARGS__)

#define SUP_ADD_CHILD0(sup, name, strat, func) \
    __sup_add_child(self, name, sup, strat, func, 0)

#define SUP_REMOVE_CHILD(sup, pid) \
    REQUEST(sup, &pid, sizeof(Pid), MAKE_STR_TAG("supervisor::remove"))
