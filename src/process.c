#include <process.h>

int process_init(Process *process, Task task, process_priority priority, Pid pid, Pid parent_pid) {
    if (!task || !process)
        return -1;
    if (priority < PROCESS_PRIORITY_LOW || priority > PROCESS_PRIORITY_VERY_HIGH)
        return -1;

    process->pid = pid;
    process->scheduler_pid = ~0;
    process->state = PROCESS_READY;
    process->priority = priority;
    process->task = task;

    links_t *link = malloc(sizeof(links_t));
    if (!link)
        return -1;

    link->other_pid = parent_pid;
    link->type = LINK_TYPE_TO_PARENT;
    link->next = NULL;
    process->links = link;

    process->next = NULL;

    if (heap_create(&process->heap, MINIMAL_HEAP_POOL_SIZE) != 0)
        return -1;

    return 0;
}

void process_destroy(Process *process) {
    if (!process)
        return;

    heap_destroy(&process->heap);

    links_t *link = process->links;
    while (link) {
        links_t *next = link->next;
        free(link);
        link = next;
    }
}
