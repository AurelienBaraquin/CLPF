#include <process.h>
#include <clpf_internal.h>
#include <links.h>

int process_init(Process *process, Task task, process_priority priority, Pid pid, Pid parent_pid) {
    if (!task || !process)
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

    process->task = task;

    if (heap_create(&process->heap, MINIMAL_HEAP_POOL_SIZE) != 0)
        return -1;

    process->links = NULL;
    link_add(pid, parent_pid, LINK_TYPE_TO_PARENT);
    if (parent_pid != -1)
        link_add(parent_pid, pid, LINK_TYPE_TO_CHILD);

    return 0;
}

void process_destroy(Process *process) {
    if (!process)
        return;

    heap_destroy(&process->heap);
    link_clear(process->links);
}
