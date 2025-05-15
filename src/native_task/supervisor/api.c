#include <native_task/supervisor.h>
#include <api/message.h>

Pid __sup_add_child(Process *self, Pid sup, SUP_STRAT strat, void (*func)(), int argc, ...) {
    Funcall_t taskcall;

    va_list ap;
    va_start(ap, argc);
    funcall_init_vargs(&taskcall, func, argc, ap);
    va_end(ap);

    if (taskcall.func == NULL) {
        write(STDERR_FILENO, "Error: Invalid taskcall\n", 25);
        return -1;
    }

    SupChildSpec spec;
    spec.magic = MAGIC_SUP_CHILD;
    spec.taskcall = taskcall;
    spec.priority = PROCESS_PRIORITY_MEDIUM;
    spec.restart = RES_PERMANENT;

    Pid *pid = REQUEST(sup, &spec, sizeof(SupChildSpec), MAKE_STR_TAG("supervisor::spawn"));
    if (pid) {
        Pid child_pid = *pid;
        safe_heap_free(&self->heap, pid);
        return child_pid;
    } else {
        write(STDERR_FILENO, "Error: Failed to spawn child process\n", 38);
        return -1;
    }
}
