#pragma once

#include <api/message.h>
#include <clpf_const.h>

#define SPAWN_PRIO_NORET(func, prio, argc, ...) \
    do { \
        Funcall_t funcall; \
        funcall_init(&funcall, func, argc, __VA_ARGS__); \
        __clpf_request_task_add(self, false, &funcall, prio, self->pid); \
    } while (0)

#define SPAWN_PRIO_NORET0(func, prio) \
    do { \
        Funcall_t funcall; \
        funcall_init(&funcall, func, 0); \
        __clpf_request_task_add(self, false, &funcall, prio, self->pid); \
    } while (0)

#define SPAWN_NORET(func, argc, ...) \
    do { \
        Funcall_t funcall; \
        funcall_init(&funcall, func, argc, __VA_ARGS__); \
        __clpf_request_task_add(self, false, &funcall, PROCESS_PRIORITY_MEDIUM, self->pid); \
    } while (0)

#define SPAWN_NORET0(func) \
    do { \
        Funcall_t funcall; \
        funcall_init(&funcall, func, 0); \
        __clpf_request_task_add(self, false, &funcall, PROCESS_PRIORITY_MEDIUM, self->pid); \
    } while (0)

#define SPAWN_PRIO(func, prio, argc, ...) \
    ({ \
        Funcall_t funcall; \
        funcall_init(&funcall, func, argc, __VA_ARGS__); \
        __clpf_request_task_add(self, true, &funcall, prio, self->pid); \
        Pid *__pid_ptr = (Pid *)RECEIVE(CORE_MESSAGE_TYPE_SPAWN_TAG); \
        Pid __pid = *__pid_ptr; \
        safe_heap_free(&self->heap, __pid_ptr); \
        __pid; \
    })

#define SPAWN0_PRIO(func, prio) \
    ({ \
        Funcall_t funcall; \
        funcall_init(&funcall, func, 0); \
        __clpf_request_task_add(self, true, &funcall, prio, self->pid); \
        Pid *__pid_ptr = (Pid *)RECEIVE(CORE_MESSAGE_TYPE_SPAWN_TAG); \
        Pid __pid = *__pid_ptr; \
        safe_heap_free(&self->heap, __pid_ptr); \
        __pid; \
    })

#define SPAWN(func, argc, ...) \
    ({ \
        Funcall_t funcall; \
        funcall_init(&funcall, func, argc, __VA_ARGS__); \
        __clpf_request_task_add(self, true, &funcall, PROCESS_PRIORITY_MEDIUM, self->pid); \
        Pid *__pid_ptr = (Pid *)RECEIVE(CORE_MESSAGE_TYPE_SPAWN_TAG); \
        Pid __pid = *__pid_ptr; \
        safe_heap_free(&self->heap, __pid_ptr); \
        __pid; \
    })

#define SPAWN0(func) \
    ({ \
        Funcall_t funcall; \
        funcall_init(&funcall, func, 0); \
        __clpf_request_task_add(self, true, &funcall, PROCESS_PRIORITY_MEDIUM, self->pid); \
        Pid *__pid_ptr = (Pid *)RECEIVE(CORE_MESSAGE_TYPE_SPAWN_TAG); \
        Pid __pid = *__pid_ptr; \
        safe_heap_free(&self->heap, __pid_ptr); \
        __pid; \
    })

#define VSPAWN_PRIO(funcall, prio) \
    ({ \
        __clpf_request_task_add(self, true, funcall, prio, self->pid); \
        Pid *__pid_ptr = (Pid *)RECEIVE(CORE_MESSAGE_TYPE_SPAWN_TAG); \
        Pid __pid = *__pid_ptr; \
        safe_heap_free(&self->heap, __pid_ptr); \
        __pid; \
    })
