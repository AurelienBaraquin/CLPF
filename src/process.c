#include <process.h>

int process_init(Process *process, Task task, process_priority priority, size_t pid) {
    if (!task || !process)
        return -1;
    if (priority < PROCESS_PRIORITY_LOW || priority > PROCESS_PRIORITY_VERY_HIGH)
        return -1;

    process->pid = pid;
    process->state = PROCESS_READY;
    process->priority = priority;
    process->task = task;

    if (heap_create(&process->heap, MINIMAL_HEAP_POOL_SIZE) != 0)
        return -1;

    return 0;
}

void process_destroy(Process *process) {
    if (!process)
        return;

    heap_destroy(&process->heap);
}
