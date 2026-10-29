#pragma once

#include <api/message.h>
#include <clpf_const.h>

// Callbacks stay blocked while waiting for the reply, so a DOWN coming from
// the new process is only handled once the caller knows its pid
#define __SPAWN_WAIT_RESULT()                                \
    ({                                                       \
        self->callbacks_blocked++;                           \
        Pid *__pid_ptr =                                     \
            (Pid *)RECEIVE(CORE_MESSAGE_TYPE_SPAWN_TAG);     \
        self->callbacks_blocked--;                           \
        Pid __pid = *__pid_ptr;                              \
        safe_heap_free(&self->heap, __pid_ptr);              \
        __pid;                                               \
    })

#define __SPAWN_RAW(name, funcall_ptr, prio, with_monitor)   \
    ({                                                       \
        __clpf_request_task_add(self, name, true, with_monitor, \
                                funcall_ptr, prio,           \
                                self->pid);                  \
        Pid __spawned = __SPAWN_WAIT_RESULT();               \
        if ((with_monitor) && __spawned != INVALID_PID)      \
            monitor_add(&self->monitor, __spawned);          \
        __spawned;                                           \
    })

#define VSPAWN_PRIO(funcall_ptr, prio)       __SPAWN_RAW("", funcall_ptr, prio, false)
#define VSPAWN_NAMED(funcall_ptr, name, prio) __SPAWN_RAW(name, funcall_ptr, prio, false)
// Spawn and monitor in one step: the DOWN is delivered even if the process exits immediately
#define VSPAWN_MONITOR_NAMED(funcall_ptr, name, prio) __SPAWN_RAW(name, funcall_ptr, prio, true)

#define SPAWN_PRIO_NAMED(func, name, prio, argc, ...)              \
    ({                                                       \
        Funcall_t funcall;                                   \
        funcall_init(&funcall, func, argc, __VA_ARGS__);     \
        __SPAWN_RAW(name, &funcall, prio, false);            \
    })

#define SPAWN0_PRIO_NAMED(func, name, prio)                        \
    ({                                                       \
        Funcall_t funcall;                                   \
        funcall_init(&funcall, func, 0);                     \
        __SPAWN_RAW(name, &funcall, prio, false);            \
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
