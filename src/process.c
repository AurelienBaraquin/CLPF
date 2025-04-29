#include <process.h>

Process *process_create(Task task, process_priority priority) {
    if (!task)
        return NULL;

    Process *process = malloc(sizeof(Process));
    if (!process)
        return NULL;

    process->pid = ~0; // ~0 indicates that the process is not yet assigned a PID
    process->state = PROCESS_READY;
    process->priority = priority;
    process->task = task;

    if (heap_create(&process->heap, MINIMAL_HEAP_POOL_SIZE) != 0) {
        free(process);
        return NULL;
    }

    return process;
}

void process_destroy(Process *process) {
    if (!process)
        return;

    heap_destroy(&process->heap);
    free(process);
}
