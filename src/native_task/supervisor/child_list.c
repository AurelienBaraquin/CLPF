#include <native_task/supervisor.h>
#include <api/memory.h>

SupChildSlot* DEF(sup_child_list_insert, SupChildList *list, SupChildSpec *child) {
    if (!list || !child)
        return NULL;

    SupChildSlot *new_child = ALLOC(sizeof(SupChildSlot));
    if (!new_child) {
        fprintf(stderr, "[SUPERVISOR] Memory allocation failed\n");
        return NULL;
    }
    new_child->pid = INVALID_PID;
    new_child->spec = *child;
    new_child->restart_count = 0;
    new_child->restart_index = 0;
    for (int i = 0; i < SUP_RESTART_HISTORY; ++i) {
        new_child->last_restart_times[i] = 0;
    }
    TAILQ_INSERT_TAIL(list, new_child, entry);

    return new_child;
}

void DEF(sup_child_list_remove, SupChildList *list, SupChildSlot *child) {
    TAILQ_REMOVE(list, child, entry);
    FREE(child);
}

SupChildSlot *sup_child_find_by_pid(supervisor_t *sv, Pid pid) {
    SupChildSlot *child;
    TAILQ_FOREACH(child, &sv->sup_childs, entry) {
        if (child->pid == pid) {
            return child;
        }
    }
    return NULL;
}
