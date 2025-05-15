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

SUP_ST DEF(__sup_handler_spawn, Message *msg, __supervisor_t *sup) {
    SupChildSpec *spec = (SupChildSpec *)msg->payload;
    if (spec->magic != MAGIC_SUP_CHILD) {
        fprintf(stderr, "[SUPERVISOR] Invalid magic number: %x\n", spec->magic);
        return SUP_NOTHING;
    }
    if (&spec->taskcall == NULL) {
        fprintf(stderr, "[SUPERVISOR] Invalid taskcall\n");
        return SUP_NOTHING;
    }
    SupChildSlot *child = CALL(__sup_child_list_insert, &sup->sup_childs, spec);
    if (!child) {
        fprintf(stderr, "[SUPERVISOR] Failed to insert child into list\n");
        return SUP_NOTHING;
    }
    child->pid = VSPAWN_NAMED(&spec->taskcall, spec->name, spec->priority);
    if (child->pid == -1) {
        fprintf(stderr, "[SUPERVISOR] Failed to spawn child process\n");
        return SUP_NOTHING;
    }
    sup->child_count++;
    MONITOR_ADD(child->pid);
    SEND(msg->sender_pid, &child->pid, sizeof(Pid), MAKE_STR_TAG("supervisor::spawn"));
    return SUP_SUCCESS;
}

SUP_ST DEF(__sup_handler_remove, Message *msg, __supervisor_t *sup) {
    Pid pid = *(Pid *)msg->payload;
    if (pid == -1) {
        fprintf(stderr, "[SUPERVISOR] Invalid PID: %ld\n", pid);
        return SUP_NOTHING;
    }
    CALL(__sup_child_list_remove, &sup->sup_childs, pid);
    sup->child_count--;
    MONITOR_REMOVE(pid);
    return SUP_SUCCESS;
}

SUP_ST DEF(__sup_handler_stop, Message *msg, __supervisor_t *sup) {
    return SUP_STOP;
}



const static struct __sv_handler_entry __sv_handlers[] = {
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

    fprintf(stderr, "[SUPERVISOR] Unknown message type: %s\n", msg->tag.str_value);
    return SUP_NOTHING;
}
