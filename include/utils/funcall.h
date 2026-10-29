#pragma once

#include <stdint.h>
#include <stdarg.h>

typedef struct {
    void (*func)();
    uintptr_t args[6];
    int argc;
} Funcall_t;

extern void call_raw_func(void (*func)(), uintptr_t *args, int argc);

void funcall_init(Funcall_t *args, void (*func)(), int argc, ...);
void funcall_init_vargs(Funcall_t *args, void (*func)(), int argc, va_list ap);
void funcall_destroy(Funcall_t *args);

void funcall_call(Funcall_t *args);
