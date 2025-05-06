#include <utils/funcall.h>
#include <stdio.h>

void funcall_init(Funcall_t *args, void (*func)(), int argc, ...) {
    if (!args || !func)
        return;

    if (argc > 5) {
        fprintf(stderr, "Warning: funcall_init: argc > 5, truncating to 5\n");
        argc = 5;
    }

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
    if (!args)
        return;

    call_raw_func(args->func, args->args, args->argc);
}
