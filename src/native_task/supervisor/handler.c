#include <native_task/supervisor.h>
#include <api/task.h>
#include <api/message.h>
#include <api/registry.h>
#include <api/monitor.h>
#include <api/link.h>

typedef SUP_ST (*supervisor_handler_t)(Process *self, Message *msg, supervisor_t *sup);

struct sv_handler_entry {
    const char *tag;
    supervisor_handler_t handler;
};

// Every spawn request gets an answer, INVALID_PID on failure, so the caller never blocks
SUP_ST DEF(sup_handler_spawn, Message *msg, supervisor_t *sup) {
    Pid child_pid = INVALID_PID;
    SupChildSpec *spec = (SupChildSpec *)msg->payload;

    if (msg->size < sizeof(SupChildSpec) || spec->magic != MAGIC_SUP_CHILD ||
        msg->size != sizeof(SupChildSpec) + spec->arg_block_size) {
        fprintf(stderr, "[SUPERVISOR] Invalid child spec\n");
    } else {
        SupChildSlot *child = CALL(sup_child_list_insert, &sup->sup_childs, spec, spec + 1);
        if (child) {
            if (!CALL(sup_start_child, child)) {
                fprintf(stderr, "[SUPERVISOR] Failed to spawn child process\n");
                CALL(sup_child_list_remove, &sup->sup_childs, child);
            } else {
                sup->child_count++;
                child_pid = child->pid;
            }
        }
    }

    SEND(msg->sender_pid, &child_pid, sizeof(Pid), MAKE_ATOM_TAG("supervisor::spawn"));
    return child_pid == INVALID_PID ? SUP_NOTHING : SUP_SUCCESS;
}

SUP_ST DEF(sup_handler_remove, Message *msg, supervisor_t *sup) {
    int removed = -1;
    SupChildSlot *child = msg->size == sizeof(Pid) ? sup_child_find_by_pid(sup, *(Pid *)msg->payload) : NULL;

    if (child) {
        DEMONITOR(child->pid);
        UNLINK(child->pid);
        CALL(sup_child_list_remove, &sup->sup_childs, child);
        sup->child_count--;
        removed = 0;
    }

    SEND(msg->sender_pid, &removed, sizeof(int), MAKE_ATOM_TAG("supervisor::remove"));
    return removed == 0 ? SUP_SUCCESS : SUP_NOTHING;
}

static const struct sv_handler_entry sv_handlers[] = {
    {"supervisor::spawn", sup_handler_spawn},
    {"supervisor::remove", sup_handler_remove},
};

SUP_ST DEF(supervisor_handler, Message *msg, supervisor_t *sup) {
    if (!msg || !sup)
        return SUP_NOTHING;

    for (size_t i = 0; i < sizeof(sv_handlers) / sizeof(sv_handlers[0]); i++) {
        if (MSG_IS(msg, clpf_atom_tag(sv_handlers[i].tag))) {
            return sv_handlers[i].handler(self, msg, sup);
        }
    }

    fprintf(stderr, "[SUPERVISOR] Unknown message (tag kind %d)\n", msg->tag.kind);
    return SUP_NOTHING;
}
