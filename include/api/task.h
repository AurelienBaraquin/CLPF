#pragma once

#include <api/message.h>
#include <clpf_const.h>

// Spawning is synchronous and does not yield: the pid is known before the
// new process can run, exit or send anything
#define CLPF_SPAWN_RAW(name, funcall_ptr, prio, with_monitor) \
    clpf_spawn(self, name, funcall_ptr, prio, with_monitor)

#define VSPAWN_PRIO(funcall_ptr, prio)       CLPF_SPAWN_RAW("", funcall_ptr, prio, false)
#define VSPAWN_NAMED(funcall_ptr, name, prio) CLPF_SPAWN_RAW(name, funcall_ptr, prio, false)
// Spawn and monitor in one step: the DOWN is delivered even if the process exits immediately
#define VSPAWN_MONITOR_NAMED(funcall_ptr, name, prio) CLPF_SPAWN_RAW(name, funcall_ptr, prio, true)

#define SPAWN_PRIO_NAMED(func, name, prio, argc, ...)              \
    ({                                                       \
        Funcall_t funcall;                                   \
        funcall_init(&funcall, func, argc, __VA_ARGS__);     \
        CLPF_SPAWN_RAW(name, &funcall, prio, false);            \
    })

#define SPAWN0_PRIO_NAMED(func, name, prio)                        \
    ({                                                       \
        Funcall_t funcall;                                   \
        funcall_init(&funcall, func, 0);                     \
        CLPF_SPAWN_RAW(name, &funcall, prio, false);            \
    })


#define SPAWN_PRIO(func, prio, argc, ...)              \
    SPAWN_PRIO_NAMED(func, "", prio, argc, __VA_ARGS__)

#define SPAWN0_PRIO(func, prio)                        \
    SPAWN0_PRIO_NAMED(func, "", prio)

#define SPAWN(func, argc, ...)                               \
    SPAWN_PRIO(func, PROCESS_PRIORITY_MEDIUM, argc, __VA_ARGS__)

#define SPAWN0(func)                                         \
    SPAWN0_PRIO(func, PROCESS_PRIORITY_MEDIUM)

#define SPAWN_NAMED(func, name, argc, ...)                   \
    SPAWN_PRIO_NAMED(func, name, PROCESS_PRIORITY_MEDIUM, argc, __VA_ARGS__)

#define SPAWN0_NAMED(func, name)                             \
    SPAWN0_PRIO_NAMED(func, name, PROCESS_PRIORITY_MEDIUM)
