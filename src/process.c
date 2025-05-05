#include <process.h>
#include <clpf_internal.h>
#include <links.h>
#include <string.h>
#include <stdio.h>

int process_init(Process *process, Funcall_t *taskcall, process_priority priority, Pid pid, Pid parent_pid) {
    if (!taskcall || !process)
        return -1;
    if (priority < PROCESS_PRIORITY_LOW || priority > PROCESS_PRIORITY_VERY_HIGH)
        return -1;

    process->pid = pid;
    process->scheduler_pid = ~0;
    process->state = PROCESS_READY;
    process->priority = priority;

    switch (priority) {
        case PROCESS_PRIORITY_LOW:
            process->schedule_count = 8;
            break;
        case PROCESS_PRIORITY_MEDIUM:
        case PROCESS_PRIORITY_HIGH:
        case PROCESS_PRIORITY_VERY_HIGH:
            process->schedule_count = 1;
            break;
        default:
            return -1;
    } 

    Funcall_t funcall = {0};
    funcall.func = taskcall->func;
    funcall.argc = taskcall->argc + 1;
    funcall.args[0] = (uintptr_t)process;
    memcpy(&funcall.args[1], taskcall->args, sizeof(uintptr_t) * (taskcall->argc));

    process->taskcall = funcall;

    process->coro = co_create(STACK_SIZE, co_entrypoint);
    if (!process->coro)
        return -1;

    if (heap_create(&process->heap, MINIMAL_HEAP_POOL_SIZE) != 0)
        return -1;

    process->links = NULL;
    link_add(pid, parent_pid, LINK_TYPE_TO_PARENT);
    if (parent_pid != -1)
        link_add(parent_pid, pid, LINK_TYPE_TO_CHILD);
    
    mpscq_init(&process->signal_queue);
    mailbox_init(&process->mailbox);

    return 0;
}

void process_destroy(Process *process) {
    if (!process)
        return;

    funcall_destroy(&process->taskcall);
    heap_destroy(&process->heap);
    co_delete(process->coro);
    link_clear(process->links);
    mpscq_clear(&process->signal_queue, NULL);
    mailbox_destroy(&process->mailbox);
}

Pid process_get_parent_pid(Process *self) {
    if (!self)
        return -1;

    links_t *link = self->links;
    while (link) {
        if (link->type == LINK_TYPE_TO_PARENT) {
            return link->other_pid;
        }
        link = link->next;
    }

    fprintf(stderr, "[%ld] No parent found\n", self->pid);
    return -1;
}
