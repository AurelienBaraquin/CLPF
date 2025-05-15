#include <api/message.h>
#include <api/monitor.h>
#include <api/task.h>
#include <native_task/supervisor.h>
#include <utils/ox.h>
#include <api/io.h>

typedef enum __supervisor_state__e {
    SUP_NOTHING,
    SUP_STOP,
    SUP_SUCCESS,
} SUP_ST;

typedef struct SupChildSlot {
    Pid pid;
    SupChildSpec spec;
    int restart_count;

    TAILQ_ENTRY(SupChildSlot) entry;
} SupChildSlot;

typedef TAILQ_HEAD(SupChildList, SupChildSlot) SupChildList;

void DEF(on_signal_down, Pid from, unsigned int reason) {
    PRINT("Process %ld received signal down from %ld with reason %u\n", self->pid, from, reason);
}

static SupChildSlot* DEF(sup_child_list_insert, SupChildList *list, SupChildSpec *child) {
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

static void DEF(sup_child_list_remove, SupChildList *list, Pid pid) {
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

static SUP_ST DEF(supervisor_compute_message, Message *msg, SupChildList *sup) {
    if (!msg)
        return SUP_NOTHING;

    if (UNLIKELY(MessageMatchFn_SameTag(msg, MAKE_STR_TAG("core::event::stop")))) {
        return SUP_STOP;
    }

    if (MessageMatchFn_SameTag(msg, MAKE_STR_TAG("supervisor::spawn"))) {
        SupChildSpec *spec = (SupChildSpec *)msg->payload;
        if (spec->magic != MAGIC_SUP_CHILD) {
            fprintf(stderr, "[SUPERVISOR] Invalid magic number: %x\n", spec->magic);
            return SUP_NOTHING;
        }
        if (spec->taskcall == NULL) {
            fprintf(stderr, "[SUPERVISOR] Invalid taskcall\n");
            return SUP_NOTHING;
        }
        SupChildSlot *child = CALL(sup_child_list_insert, sup, spec);
        if (!child) {
            fprintf(stderr, "[SUPERVISOR] Failed to insert child into list\n");
            return SUP_NOTHING;
        }
        child->pid = VSPAWN_PRIO(spec->taskcall, spec->priority);
        if (child->pid == -1) {
            fprintf(stderr, "[SUPERVISOR] Failed to spawn child process\n");
            return SUP_NOTHING;
        }
        MONITOR_ADD(child->pid);
        SEND(msg->sender_pid, &child->pid, sizeof(Pid), MAKE_STR_TAG("supervisor::spawn"));
        return SUP_SUCCESS;
    }

    if (MessageMatchFn_SameTag(msg, MAKE_STR_TAG("supervisor::remove"))) {
        Pid pid = *(Pid *)msg->payload;
        if (pid == -1) {
            fprintf(stderr, "[SUPERVISOR] Invalid PID: %ld\n", pid);
            return SUP_NOTHING;
        }
        CALL(sup_child_list_remove, sup, pid);
        MONITOR_REMOVE(pid);
        return SUP_SUCCESS;
    }

}

TASK0(__supervisor__) {
    MONITOR_INIT(on_signal_down);
    Message *msg = NULL;
    bool running = true;

    SupChildList sup_childs;
    TAILQ_INIT(&sup_childs);

    while (running) {
        msg = RECEIVE_RAW(NO_TAG);

        SUP_ST r = CALL(supervisor_compute_message, msg, &sup_childs);

        switch (r) {
            case SUP_NOTHING:
                break;
            case SUP_STOP:
                running = false;
                break;
            case SUP_SUCCESS:
                if (msg->payload)
                    FREE(msg->payload);
                FREE(msg);
                break;
            
            default:
                break;
        }
    }
    EXIT();
}
