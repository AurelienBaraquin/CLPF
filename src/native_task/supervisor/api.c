#include <api/supervisor.h>
#include <api/memory.h>

Pid clpf_sup_add_child(Process *self, char *name, Pid sup, RestartType restart, process_priority prio, void (*func)(), int argc, ...) {
    Funcall_t taskcall;

    va_list ap;
    va_start(ap, argc);
    funcall_init_vargs(&taskcall, func, argc, ap);
    va_end(ap);

    if (taskcall.func == NULL) {
        write(STDERR_FILENO, "Error: Invalid taskcall\n", 25);
        return INVALID_PID;
    }

    SupChildSpec spec = {0};
    spec.magic = MAGIC_SUP_CHILD;
    strncpy(spec.name, name, NAME_MAX_LENGTH - 1);
    spec.taskcall = taskcall;
    spec.priority = prio;
    spec.restart = restart;

    Message *reply = REQUEST(sup, &spec, sizeof(SupChildSpec), MAKE_STR_TAG("supervisor::spawn"));
    if (!reply)
        return INVALID_PID;

    Pid child_pid = *(Pid *)reply->payload;
    MSG_FREE(reply);
    return child_pid;
}

int clpf_sup_remove_child(Process *self, Pid sup, Pid child) {
    Message *reply = REQUEST(sup, &child, sizeof(Pid), MAKE_STR_TAG("supervisor::remove"));
    if (!reply)
        return -1;

    int result = *(int *)reply->payload;
    MSG_FREE(reply);
    return result;
}
