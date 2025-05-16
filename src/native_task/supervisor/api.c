#include <native_task/supervisor.h>
#include <api/message.h>

Pid __sup_add_child(Process *self, ProcessName name, Pid sup, SUP_STRAT strat, process_priority prio, void (*func)(), int argc, ...) {
    Funcall_t taskcall;

    va_list ap;
    va_start(ap, argc);
    funcall_init_vargs(&taskcall, func, argc, ap);
    va_end(ap);

    if (taskcall.func == NULL) {
        write(STDERR_FILENO, "Error: Invalid taskcall\n", 25);
        return -1;
    }

    SupChildSpec spec = {0};
    spec.magic = MAGIC_SUP_CHILD;
    strncpy(spec.name, name, NAME_MAX_LENGTH);
    spec.taskcall = taskcall;
    spec.priority = prio;
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
