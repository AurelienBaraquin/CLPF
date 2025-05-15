#include <native_task/supervisor.h>
#include <api/memory.h>

SupChildSlot* DEF(__sup_child_list_insert, SupChildList *list, SupChildSpec *child) {
    if (!list || !child)
        return NULL;

    SupChildSlot *new_child = ALLOC(sizeof(SupChildSlot));
    if (!new_child) {
        fprintf(stderr, "[SUPERVISOR] Memory allocation failed\n");
        return NULL;
    }
    new_child->pid = -1;
    new_child->spec = *child;
    new_child->restart_count = 0;
    TAILQ_INSERT_TAIL(list, new_child, entry);

    return new_child;
}

void DEF(__sup_child_list_remove, SupChildList *list, Pid pid) {
    if (!list)
        return;

    SupChildSlot *child;
    TAILQ_FOREACH(child, list, entry) {
        if (child->pid == pid) {
            TAILQ_REMOVE(list, child, entry);
            FREE(child);
            break;
        }
    }
}
