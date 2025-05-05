#include <utils/funcall.h>

#define NULL ((void*)0)

void funcall_init(Funcall_t *args, void (*func)(), int argc, ...) {
    if (!args || argc <= 0)
        return;
    
    args->func = func;

    va_list ap;
    va_start(ap, argc);

    for (int i = 0; i < argc; i++)
        args->args[i] = va_arg(ap, uintptr_t);

    va_end(ap);
    args->argc = argc;
}

void funcall_destroy(Funcall_t *args) {
    if (!args)
        return;

    args->func = NULL;
    args->argc = 0;
    for (int i = 0; i < 6; i++) {
        args->args[i] = 0;
    }
}

void funcall_call(Funcall_t *args) {
    if (!args || args->argc <= 0)
        return;

    call_raw_func(args->func, args->args, args->argc);
}
