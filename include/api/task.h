#pragma once

#include <clpf_internal.h>

#define SPAWN_NORET(func, argc, ...) \
    do { \
        Funcall_t funcall; \
        funcall_init(&funcall, func, argc, __VA_ARGS__); \
        __clpf_request_task_add(self, false, &funcall, PROCESS_PRIORITY_MEDIUM, self->pid); \
    } while (0)

#ifdef INCLUDE_CLPF_MESSAGE
#define SPAWN SPAWN_NORET
#else
#include <api/message.h>

#define SPAWN(func, argc, ...) \
    ({ \
        Funcall_t funcall; \
        funcall_init(&funcall, func, argc, __VA_ARGS__); \
        __clpf_request_task_add(self, true, &funcall, PROCESS_PRIORITY_MEDIUM, self->pid); \
        Pid *__pid_ptr = (Pid *)RECEIVE(NULL, NULL); \
        Pid __pid = *__pid_ptr; \
        free(__pid_ptr); \
        __pid; \
    })
#endif
