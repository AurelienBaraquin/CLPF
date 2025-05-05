#pragma once

#include <clpf_internal.h>

#define SPAWN(func, argc, ...) \
    do { \
        Funcall_t funcall; \
        funcall_init(&funcall, func, argc, __VA_ARGS__); \
        __clpf_request_task_add(&funcall, PROCESS_PRIORITY_MEDIUM, self->pid); \
    } while (0)
