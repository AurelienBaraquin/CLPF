#pragma once

#include <api/message.h>
#include <clpf_const.h>

#define __SPAWN_WAIT_RESULT()                                \
    ({                                                       \
        Pid *__pid_ptr =                                     \
            (Pid *)RECEIVE(CORE_MESSAGE_TYPE_SPAWN_TAG);     \
        Pid __pid = *__pid_ptr;                              \
        safe_heap_free(&self->heap, __pid_ptr);              \
        __pid;                                               \
    })

#define __SPAWN_RAW(name, funcall_ptr, prio)                 \
    ({                                                       \
        __clpf_request_task_add(self, name, true,            \
                                funcall_ptr, prio,           \
                                self->pid);                  \
        __SPAWN_WAIT_RESULT();                               \
    })

#define VSPAWN_PRIO(funcall_ptr, prio)       __SPAWN_RAW("", funcall_ptr, prio)
#define VSPAWN_NAMED(funcall_ptr, name, prio) __SPAWN_RAW(name, funcall_ptr, prio)

#define SPAWN_PRIO_NAMED(func, name, prio, argc, ...)              \
    ({                                                       \
        Funcall_t funcall;                                   \
        funcall_init(&funcall, func, argc, __VA_ARGS__);     \
        __SPAWN_RAW(name, &funcall, prio);                   \
    })

#define SPAWN0_PRIO_NAMED(func, name, prio)                        \
    ({                                                       \
        Funcall_t funcall;                                   \
        funcall_init(&funcall, func, 0);                     \
        __SPAWN_RAW(name, &funcall, prio);                   \
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
