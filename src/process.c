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

    if (safe_heap_create(&process->heap, MINIMAL_HEAP_POOL_SIZE) != 0)
        return -1;

    links_init(&process->links);

    if (parent_pid != -1) {
        process->links.parent = parent_pid;
        Process *parent = &clpf.process_table[parent_pid];
        link_add_child(&parent->links, pid);
    }

    mpscq_init(&process->signal_queue);
    mailbox_init(&process->mailbox);

    return 0;
}

void process_destroy(Process *process) {
    if (!process)
        return;

    mailbox_destroy(&process->mailbox, (heap_t *)&process->heap);
    funcall_destroy(&process->taskcall);
    safe_heap_destroy(&process->heap);
    co_delete(process->coro);
    links_clear(&process->links);
    mpscq_clear(&process->signal_queue, NULL);
}

Pid process_get_parent_pid(Process *self) {
    return self ? self->links.parent : -1;
}

size_t process_get_child_count(Process *self) {
    return self ? link_count(self->links.children) : 0;
}

Pid process_get_child_pid(Process *self, size_t index) {
    return self ? link_get_nth(self->links.children, index) : -1;
}

bool process_poll_mailbox(Process* self) {
    if (!self)
        return false;

    mailbox_transfer_inbox_to_seen(&self->mailbox);

    if (self->mailbox.seen_head != NULL) {
        self->state = PROCESS_READY;
        return true;
    }

    return false;
}
