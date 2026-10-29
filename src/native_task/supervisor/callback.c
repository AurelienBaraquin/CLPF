#include <api/message.h>
#include <native_task/supervisor.h>
#include <api/io.h>
#include <api/monitor.h>
#include <utils/time.h>
#include <api/task.h>
#include <api/registry.h>

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

static bool DEF(restart_child_impl, supervisor_t *sv, SupChildSlot *child) {
    uint64_t now = now_ms();

    // Insert the timestamp in a circular manner
    child->last_restart_times[child->restart_index] = now;
    child->restart_index = (child->restart_index + 1) % SUP_RESTART_HISTORY;

    // Count how many restarts occurred within the `period_ms` window
    int count = 0;
    for (int i = 0; i < SUP_RESTART_HISTORY; ++i) {
        if (now - child->last_restart_times[i] <= sv->period_ms)
            count++;
    }

    if (count > sv->max_restarts) {
        PRINT("Child '%s' restarted too often, giving up\n", child->spec.name);
        return false;
    }

    Pid new_pid = VSPAWN_MONITOR_NAMED(&child->spec.taskcall, child->spec.name, child->spec.priority);
    if (new_pid == INVALID_PID) {
        PRINT("Failed to restart child '%s'\n", child->spec.name);
        return false;
    }

    child->pid = new_pid;
    child->restart_count++;
    return true;
}

static void DEF(forget_child, supervisor_t *sv, SupChildSlot *child) {
    CALL(sup_child_list_remove, &sv->sup_childs, child);
    sv->child_count--;
}

// Only SUP_ONE_FOR_ONE reaches this point: the supervisor refuses the other strategies
void DEF(on_child_down, Pid from, unsigned int reason, supervisor_t *sv) {
    SupChildSlot *child = sup_child_find_by_pid(sv, from);
    if (!child) return;

    if (!should_restart_child(child, reason) || !CALL(restart_child_impl, sv, child))
        CALL(forget_child, sv, child);
}
