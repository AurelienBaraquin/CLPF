#include <process.h>
#include <clpf_internal.h>
#include <links.h>
#include <string.h>
#include <stdio.h>
#ifdef __SANITIZE_ADDRESS__
#include <sanitizer/asan_interface.h>
#endif

int process_init(Process *process, Funcall_t *taskcall, const SpawnOptions *options, Pid pid, Pid parent_pid) {
    process_priority priority = options->priority;
    if (!taskcall || !process)
        return -1;
    if ((unsigned)priority > PROCESS_PRIORITY_VERY_HIGH)
        return -1;
    if (options->arg_block && taskcall->argc != 0) {
        fprintf(stderr, "[process_init] An argument block replaces the integer arguments\n");
        return -1;
    }

    process->pid = pid;
    memset(process->name, 0, NAME_MAX_LENGTH);
    if (options->name)
        strncpy(process->name, options->name, NAME_MAX_LENGTH - 1);
    memset(process->registered_name, 0, NAME_MAX_LENGTH);

    process->scheduler_pid = -1;
    process->state = PROCESS_INIT;
    process->priority = priority;
    atomic_store_explicit(&process->runs, 0, memory_order_relaxed);
    process->group_count = 0;
    process->hibernating = false;
    process->table_count = 0;
    process->bin_holds = NULL;
    process->wait_match = NULL;

    process->schedule_count = priority == PROCESS_PRIORITY_LOW ? LOW_PRIORITY_SKIPS : 0;
    process->reductions = REDUCTION_BUDGET;

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
    process->stack_size = options->stack_size ? options->stack_size : CLPF_DEFAULT_STACK_SIZE;
    process->mailbox_limit = options->mailbox_limit;
    atomic_store(&process->queued, 0);

    if (safe_heap_create(&process->heap, MINIMAL_HEAP_POOL_SIZE) != 0)
        return -1;
    process->heap.max_size = options->max_heap_size;

    if (options->arg_block) {
        void *block = safe_heap_alloc(&process->heap, options->arg_block_size);
        if (!block) {
            safe_heap_destroy(&process->heap);
            return -1;
        }
        memcpy(block, options->arg_block, options->arg_block_size);
        process->taskcall.args[1] = (uintptr_t)block;
        process->taskcall.argc = 2;
    }

    links_init(&process->links);
    monitor_init(&process->monitor);

    process->links.parent = parent_pid;
    process->exit_notified = false;
    process->linked = NULL;
    process->trap_exit = options->trap_exit;
    process->awaited_reply = 0;
    process->system = options->system;
    process->entry = options->entry;
    process->exit_reason = 0;
    atomic_store(&process->kill_pending, false);
    process->kill_reason = 0;
    atomic_store(&process->wake_pending, false);

    mailbox_init(&process->mailbox);

#ifdef CLPF_TRACE
    printf("[%s (%ld)] created\n", process->name, process->pid);
#endif

    return 0;
}

void process_release_stack(Process *process) {
    if (!process->share_stack)
        return;
#ifdef __SANITIZE_ADDRESS__
    // AddressSanitizer does not know about coroutine stacks: clear the redzones
    // their frames left, or the next process on this stack looks overflowed
    ASAN_UNPOISON_MEMORY_REGION(process->share_stack->real_ptr, process->share_stack->real_sz);
#endif
    stack_release(process->share_stack, process->stack_size);
    process->share_stack = NULL;
}

void process_destroy(Process *process) {
    if (!process || process_slot_free(process))
        return;

#ifdef CLPF_TRACE
    printf("[%s (%ld)] destroyed\n", process->name, process->pid);
#endif

    // From here on, senders see the slot as free and never reach the heap
    pthread_mutex_lock(&process->lock);
    process->pid = PID_FREE;
    pthread_mutex_unlock(&process->lock);

    // The nodes live in the heap: release the binaries before it goes
    bin_holds_release_all(process->bin_holds);
    process->bin_holds = NULL;
    mailbox_destroy(&process->mailbox);
    funcall_destroy(&process->taskcall);
    safe_heap_destroy(&process->heap);
    if (process->acoro)
        aco_destroy(process->acoro);
    process_release_stack(process);
    links_clear(&process->links);
    free_link_list(process->linked);
    process->linked = NULL;
    monitor_clear(&process->monitor);
}

Process *process_lock(Pid pid) {
    if (pid <= 0)
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

void process_notify_locked(Process *process) {
    // Woken up: until it waits again, every message may concern it
    process->wait_match = NULL;
    // An exiting process is never queued again, so its slot can be freed safely
    if (process->exit_notified)
        return;
    if (atomic_exchange(&process->wake_pending, true))
        return;

    Scheduler *scheduler = &clpf.scheduler_table[process->scheduler_pid];
    mpscq_push(&scheduler->wakeups, &process->wake_node);
    scheduler_wake(scheduler);
}

Pid process_get_parent_pid(Process *self) {
    return self ? self->links.parent : -1;
}

size_t process_get_child_count(Process *self) {
    if (!self)
        return 0;

    pthread_mutex_lock(&self->lock);
    size_t count = self->links.child_count;
    pthread_mutex_unlock(&self->lock);
    return count;
}

Pid process_get_child_pid(Process *self, size_t index) {
    if (!self)
        return INVALID_PID;

    pthread_mutex_lock(&self->lock);
    Pid child = link_get_nth_child(&self->links, index);
    pthread_mutex_unlock(&self->lock);
    return child;
}
