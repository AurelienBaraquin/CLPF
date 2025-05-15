#include <api/message.h>
#include <native_task/supervisor.h>
#include <api/io.h>
#include <api/monitor.h>
#include <utils/time.h>
#include <api/task.h>
#include <api/tag.h>

static bool should_restart_child(SupChildSlot *child, unsigned int reason) {
    switch (child->spec.restart) {
        case RES_PERMANENT:
            return true;
        case RES_TRANSIENT:
            return reason != EXIT_REASON_NORMAL;
        default:
            return false;
    }
}

static bool DEF(restart_child_impl, __supervisor_t *sv, SupChildSlot *child) {
    uint64_t now = now_ms();

    // Insert the timestamp in a circular manner
    child->last_restart_times[child->restart_index] = now;
    child->restart_index = (child->restart_index + 1) % 10;

    // Count how many restarts occurred within the `period_ms` window
    int count = 0;
    for (int i = 0; i < 10; ++i) {
        if (now - child->last_restart_times[i] <= sv->period_ms)
            count++;
    }

    if (count > sv->max_restarts) {
        return false;
    }

    if (!&child->spec.taskcall) {
        PRINT("Child '%s' has no taskcall. Cannot restart.\n", child->pid);
        return false;
    }

    Pid new_pid = VSPAWN_NAMED(&child->spec.taskcall, child->spec.name, child->spec.priority);
    if (new_pid == INVALID_PID) {
        PRINT("Failed to restart child '%s'\n", child->pid);
        return false;
    }

    child->pid = new_pid;
    child->restart_count++;
    MONITOR_ADD(child->pid);
    return true;
}

static void DEF(restart_child, __supervisor_t *sv, SupChildSlot *child) {
    if (CALL(restart_child_impl, sv, child) == false) {
        monitor_remove(&self->monitor, child->pid);
        CALL(__sup_child_list_remove, &sv->sup_childs, child->pid);
        sv->child_count--;
    }
}

void DEF(on_signal_down, Pid from, unsigned int reason, __supervisor_t *sv) {
    SupChildSlot *child = CALL(__sup_child_find_by_pid, sv, from);
    if (!child) return;

    if (!should_restart_child(child, reason)) {
        MONITOR_REMOVE(child->pid);
        return;
    }

    switch (sv->strategy) {
        case SUP_ONE_FOR_ONE:
            CALL(restart_child, sv, child);
            break;
        case SUP_ONE_FOR_ALL:
            child = NULL;
            TAILQ_FOREACH(child, &sv->sup_childs, entry) {
                if (child->pid != from) {
                    CALL(restart_child, sv, child);
                }
            }
            break;
        case SUP_REST_FOR_ONE: {
            SupChildSlot *current_child = child;
            while (current_child != NULL) {
                if (current_child->pid != from) {
                    CALL(restart_child, sv, current_child);
                }
                current_child = TAILQ_NEXT(current_child, entry);
            }
            break;
        }
    }
}
