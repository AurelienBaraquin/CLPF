#pragma once

#include <native_task/supervisor.h>
#include <api/task.h>
#include <api/message.h>
#include <api/tag.h>

#define SPAWN_SUPERVISOR(strat) \
    SPAWN(__supervisor__, 1, strat); \

Pid __sup_add_child(Process *self, Pid sup, SUP_STRAT strat, void (*func)(), int argc, ...);

#define SUP_ADD_CHILD(sup, strat, func, argc, ...) \
    __sup_add_child(self, sup, strat, func, argc, __VA_ARGS__)

#define SUP_ADD_CHILD0(sup, strat, func) \
    __sup_add_child(self, sup, strat, func, 0)

#define SUP_REMOVE_CHILD(sup, pid) \
    REQUEST(sup, &pid, sizeof(Pid), MAKE_STR_TAG("supervisor::remove"))
