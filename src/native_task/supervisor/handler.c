#include <native_task/supervisor.h>
#include <api/task.h>
#include <api/message.h>
#include <api/tag.h>
#include <api/monitor.h>

typedef SUP_ST (*supervisor_handler_t)(Process *self, Message *msg, __supervisor_t *sup);

struct __sv_handler_entry {
    const char *tag;
    supervisor_handler_t handler;
};

// Every spawn request gets an answer, INVALID_PID on failure, so the caller never blocks
SUP_ST DEF(__sup_handler_spawn, Message *msg, __supervisor_t *sup) {
    Pid child_pid = INVALID_PID;
    SupChildSpec *spec = (SupChildSpec *)msg->payload;

    if (msg->size != sizeof(SupChildSpec) || spec->magic != MAGIC_SUP_CHILD) {
        fprintf(stderr, "[SUPERVISOR] Invalid child spec\n");
    } else {
        SupChildSlot *child = CALL(__sup_child_list_insert, &sup->sup_childs, spec);
        if (child) {
            child->pid = VSPAWN_MONITOR_NAMED(&spec->taskcall, spec->name, spec->priority);
            if (child->pid == INVALID_PID) {
                fprintf(stderr, "[SUPERVISOR] Failed to spawn child process\n");
                CALL(__sup_child_list_remove, &sup->sup_childs, child);
            } else {
                sup->child_count++;
                child_pid = child->pid;
            }
        }
    }

    SEND(msg->sender_pid, &child_pid, sizeof(Pid), MAKE_STR_TAG("supervisor::spawn"));
    return child_pid == INVALID_PID ? SUP_NOTHING : SUP_SUCCESS;
}

SUP_ST DEF(__sup_handler_remove, Message *msg, __supervisor_t *sup) {
    int removed = -1;
    SupChildSlot *child = msg->size == sizeof(Pid) ? __sup_child_find_by_pid(sup, *(Pid *)msg->payload) : NULL;

    if (child) {
        MONITOR_REMOVE(child->pid);
        CALL(__sup_child_list_remove, &sup->sup_childs, child);
        sup->child_count--;
        removed = 0;
    }

    SEND(msg->sender_pid, &removed, sizeof(int), MAKE_STR_TAG("supervisor::remove"));
    return removed == 0 ? SUP_SUCCESS : SUP_NOTHING;
}

SUP_ST DEF(__sup_handler_stop, Message *msg, __supervisor_t *sup) {
    (void)self;
    (void)msg;
    (void)sup;
    return SUP_STOP;
}

static const struct __sv_handler_entry __sv_handlers[] = {
    {"supervisor::spawn", __sup_handler_spawn},
    {"supervisor::remove", __sup_handler_remove},
    {"core::event::stop", __sup_handler_stop},
};

SUP_ST DEF(__supervisor_handler, Message *msg, __supervisor_t *sup) {
    if (!msg || !sup)
        return SUP_NOTHING;

    for (size_t i = 0; i < sizeof(__sv_handlers) / sizeof(__sv_handlers[0]); i++) {
        if (MessageMatchFn_SameTag(msg, MAKE_STR_TAG(__sv_handlers[i].tag))) {
            return __sv_handlers[i].handler(self, msg, sup);
        }
    }

    fprintf(stderr, "[SUPERVISOR] Unknown message (tag kind %d)\n", msg->tag.kind);
    return SUP_NOTHING;
}
