#include <api/message.h>
#include <native_task/supervisor.h>
#include <api/io.h>
#include <api/monitor.h>
#include <api/link.h>
#include <utils/time.h>
#include <api/task.h>
#include <api/timer.h>

static bool should_restart(RestartType restart, unsigned int reason) {
    switch (restart) {
        case RES_PERMANENT: return true;
        case RES_TRANSIENT: return reason != EXIT_REASON_NORMAL;
        default: return false;
    }
}

bool DEF(sup_start_child, SupChildSlot *child) {
    Funcall_t taskcall = child->spec.taskcall;
    SpawnOptions options = {
        .name = child->spec.name,
        .priority = child->spec.priority,
        .monitor = true, // the DOWN drives restarts
        .link = true,    // children die with their supervisor
        .system = self->system,
    };
    child->pid = clpf_spawn(self, &taskcall, options);
    return child->pid != INVALID_PID;
}

// Asks a running child to stop (a child trapping exits gets SUP_SHUTDOWN_TIMEOUT_MS
// to finish its work), kills it past that, and consumes its DOWN so it is not taken for a crash
static void DEF(stop_child, SupChildSlot *child) {
    if (child->pid == INVALID_PID)
        return;

    SEND_EXIT(child->pid, EXIT_REASON_SHUTDOWN);
    Message *down = RECEIVE_TIMEOUT(MAKE_DOWN_TAG(child->pid), SUP_SHUTDOWN_TIMEOUT_MS);
    if (!down) {
        KILL(child->pid);
        down = RECEIVE(MAKE_DOWN_TAG(child->pid));
    }
    MSG_FREE(down);
    child->pid = INVALID_PID;
}

static void DEF(stop_from, supervisor_t *sv, SupChildSlot *first) {
    SupChildSlot *child;
    TAILQ_FOREACH_REVERSE(child, &sv->sup_childs, SupChildList, entry) {
        CALL(stop_child, child);
        if (child == first)
            break;
    }
}

void DEF(sup_stop_all, supervisor_t *sv) {
    CALL(stop_from, sv, TAILQ_FIRST(&sv->sup_childs));
}

static void DEF(forget_child, supervisor_t *sv, SupChildSlot *child) {
    CALL(sup_child_list_remove, &sv->sup_childs, child);
    sv->child_count--;
}

// Records a restart; false once more than max_restarts happened within period_ms
static bool allow_restart(supervisor_t *sv) {
    uint64_t now = now_ms();
    int slots = sv->max_restarts + 1;

    sv->restart_times[sv->restart_next] = now;
    sv->restart_next = (sv->restart_next + 1) % slots;

    int recent = 0;
    for (int i = 0; i < slots; ++i) {
        if (sv->restart_times[i] != 0 && now - sv->restart_times[i] <= sv->period_ms)
            recent++;
    }
    return recent <= sv->max_restarts;
}

// Starts again the stopped children from `first` on, in start order; the
// temporary ones the supervisor stopped are dropped instead
static void DEF(restart_from, supervisor_t *sv, SupChildSlot *first) {
    SupChildSlot *child = first;
    while (child) {
        SupChildSlot *next = TAILQ_NEXT(child, entry);
        if (child->pid == INVALID_PID) {
            if (child->spec.restart == RES_TEMPORARY || !CALL(sup_start_child, child)) {
                PRINT("[supervisor] child '%s' not restarted\n", child->spec.name);
                CALL(forget_child, sv, child);
            }
        }
        child = next;
    }
}

void DEF(sup_on_child_down, Pid from, unsigned int reason, supervisor_t *sv) {
    SupChildSlot *child = sup_child_find_by_pid(sv, from);
    if (!child)
        return;
    child->pid = INVALID_PID;

    if (!should_restart(child->spec.restart, reason)) {
        CALL(forget_child, sv, child);
        return;
    }

    // Too many restarts: stop everything and fail, so whoever watches this supervisor knows
    if (!allow_restart(sv)) {
        PRINT("[supervisor] too many restarts, shutting down\n");
        CALL(sup_stop_all, sv);
        EXIT_ERROR();
    }

    switch (sv->strategy) {
        case SUP_ONE_FOR_ONE: CALL(restart_from, sv, child); break;
        case SUP_ONE_FOR_ALL:
            CALL(sup_stop_all, sv);
            CALL(restart_from, sv, TAILQ_FIRST(&sv->sup_childs));
            break;
        case SUP_REST_FOR_ONE:
            CALL(stop_from, sv, child);
            CALL(restart_from, sv, child);
            break;
    }
}
