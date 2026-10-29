#include <process.h>
#include <clpf_internal.h>
#include <links.h>
#include <string.h>
#include <stdio.h>

int process_init(Process *process, char *name, Funcall_t *taskcall, process_priority priority, Pid pid, Pid parent_pid) {
    if (!taskcall || !process)
        return -1;
    if (priority < PROCESS_PRIORITY_LOW || priority > PROCESS_PRIORITY_VERY_HIGH)
        return -1;

    process->pid = pid;
    memset(process->__name, 0, NAME_MAX_LENGTH);
    strncpy(process->__name, name, NAME_MAX_LENGTH - 1);

    process->scheduler_pid = -1;
    process->state = PROCESS_INIT,
    process->priority = priority;

    process->schedule_count = priority == PROCESS_PRIORITY_LOW ? LOW_PRIORITY_SKIPS : 0;

    Funcall_t funcall = {0};
    funcall.func = taskcall->func;
    funcall.argc = taskcall->argc + 1;
    funcall.args[0] = (uintptr_t)process;

    if (taskcall->argc >= 6) {
        fprintf(stderr, "[process_init] Too many arguments (%d), max is 5\n", taskcall->argc);
        return -1;
    }    
    memcpy(&funcall.args[1], taskcall->args, sizeof(uintptr_t) * (taskcall->argc));

    process->taskcall = funcall;

    process->acoro = NULL;
    process->share_stack = NULL;

    if (safe_heap_create(&process->heap, MINIMAL_HEAP_POOL_SIZE) != 0)
        return -1;

    links_init(&process->links);
    monitor_init(&process->monitor);

    process->links.parent = parent_pid;
    process->exit_notified = false;
    process->callbacks_blocked = 0;

    mpscq_init(&process->signal_queue);
    mailbox_init(&process->mailbox);

#ifdef CLPF_TRACE
    printf("[%s (%ld)] created\n", process->__name, process->pid);
#endif

    return 0;
}

void process_destroy(Process *process) {
    if (!process || process->pid == INVALID_PID)
        return;

#ifdef CLPF_TRACE
    printf("[%s (%ld)] destroyed\n", process->__name, process->pid);
#endif

    // From here on, senders see the slot as free and never reach the heap
    pthread_mutex_lock(&process->lock);
    process->pid = INVALID_PID;
    pthread_mutex_unlock(&process->lock);

    mailbox_destroy(&process->mailbox);
    funcall_destroy(&process->taskcall);
    safe_heap_destroy(&process->heap);
    if (process->acoro)
        aco_destroy(process->acoro);
    if (process->share_stack)
        aco_share_stack_destroy(process->share_stack);
    links_clear(&process->links);
    monitor_clear(&process->monitor);
    mpscq_clear(&process->signal_queue, NULL);
}

Process *process_get(Pid pid) {
    if (pid < 0)
        return NULL;

    Process *process = &clpf.process_table[PID_INDEX(pid)];
    return process->pid == pid ? process : NULL;
}

Process *process_lock(Pid pid) {
    if (pid < 0)
        return NULL;

    Process *process = &clpf.process_table[PID_INDEX(pid)];
    pthread_mutex_lock(&process->lock);
    if (process->pid != pid) {
        pthread_mutex_unlock(&process->lock);
        return NULL;
    }
    return process;
}

void process_unlock(Process *process) {
    pthread_mutex_unlock(&process->lock);
}

Pid process_get_parent_pid(Process *self) {
    return self ? self->links.parent : -1;
}

size_t process_get_child_count(Process *self) {
    if (!self)
        return 0;

    pthread_mutex_lock(&self->lock);
    size_t count = link_count(self->links.children);
    pthread_mutex_unlock(&self->lock);
    return count;
}

Pid process_get_child_pid(Process *self, size_t index) {
    if (!self)
        return INVALID_PID;

    pthread_mutex_lock(&self->lock);
    Pid child = link_get_nth(self->links.children, index);
    pthread_mutex_unlock(&self->lock);
    return child;
}
