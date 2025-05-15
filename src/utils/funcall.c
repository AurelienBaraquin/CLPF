#include <utils/funcall.h>
#include <stdio.h>

void funcall_init_vargs(Funcall_t *args, void (*func)(), int argc, va_list ap) {
    if (!args || !func)
        return;

    if (argc > 5) {
        fprintf(stderr, "Warning: funcall_init_vargs: argc > 5, truncating to 5\n");
        argc = 5;
    }

    args->func = func;

    for (int i = 0; i < argc; i++)
        args->args[i] = va_arg(ap, uintptr_t);

    args->argc = argc;
}

void funcall_init(Funcall_t *args, void (*func)(), int argc, ...) {
    va_list ap;
    va_start(ap, argc);

    funcall_init_vargs(args, func, argc, ap);

    va_end(ap);
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
