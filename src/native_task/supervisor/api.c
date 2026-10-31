#include <api/supervisor.h>
#include <api/memory.h>
#include <string.h>

// Sends the spec, followed by its argument block, and returns the started child's pid
static Pid DEF(request_child, Pid sup, SupChildSpec *spec, const void *arg_block) {
    size_t size = sizeof(SupChildSpec) + spec->arg_block_size;
    char *request = ALLOC(size);
    if (!request)
        return INVALID_PID;
    memcpy(request, spec, sizeof(SupChildSpec));
    if (spec->arg_block_size > 0)
        memcpy(request + sizeof(SupChildSpec), arg_block, spec->arg_block_size);

    Message *reply = REQUEST(sup, request, size, MAKE_ATOM_TAG("supervisor::spawn"));
    FREE(request);
    if (!reply)
        return INVALID_PID;

    Pid child_pid = *(Pid *)reply->payload;
    MSG_FREE(reply);
    return child_pid;
}

Pid clpf_sup_add_child(Process *self, char *name, Pid sup, RestartType restart, process_priority prio, void (*func)(),
                       int argc, ...) {
    Funcall_t taskcall;

    va_list ap;
    va_start(ap, argc);
    funcall_init_vargs(&taskcall, func, argc, ap);
    va_end(ap);

    if (taskcall.func == NULL) {
        fprintf(stderr, "[supervisor] invalid task for child '%s'\n", name);
        return INVALID_PID;
    }

    SupChildSpec spec = {0};
    spec.magic = MAGIC_SUP_CHILD;
    strncpy(spec.name, name, NAME_MAX_LENGTH - 1);
    spec.taskcall = taskcall;
    spec.priority = prio;
    spec.restart = restart;

    return CALL(request_child, sup, &spec, NULL);
}

Pid clpf_sup_add_child_opts(Process *self, Pid sup, RestartType restart, void (*func)(), SpawnOptions options) {
    if (!func || (options.arg_block_size > 0 && !options.arg_block)) {
        fprintf(stderr, "[supervisor] invalid task or argument block for child '%s'\n",
                options.name ? options.name : "");
        return INVALID_PID;
    }

    SupChildSpec spec = {0};
    spec.magic = MAGIC_SUP_CHILD;
    if (options.name)
        strncpy(spec.name, options.name, NAME_MAX_LENGTH - 1);
    funcall_init(&spec.taskcall, func, 0);
    spec.priority = options.priority;
    spec.restart = restart;
    spec.stack_size = options.stack_size;
    spec.mailbox_limit = options.mailbox_limit;
    spec.arg_block_size = options.arg_block ? options.arg_block_size : 0;

    return CALL(request_child, sup, &spec, options.arg_block);
}

int clpf_sup_remove_child(Process *self, Pid sup, Pid child) {
    Message *reply = REQUEST(sup, &child, sizeof(Pid), MAKE_ATOM_TAG("supervisor::remove"));
    if (!reply)
        return -1;

    int result = *(int *)reply->payload;
    MSG_FREE(reply);
    return result;
}
