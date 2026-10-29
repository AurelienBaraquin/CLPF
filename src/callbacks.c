#include <api/message.h>

void handle_signals_callbacks(Process *self) {
    Message *msg;
    while ((msg = FETCH(MAKE_STR_TAG("scheduler::event::signal")))) {
        Signal *sig = msg->payload;
        switch (sig->type) {
            case SIGNAL_DOWN:
                monitor_run(&self->monitor, self, sig->from, sig->reason);
                break;
            default:
                break;
        }

        MSG_FREE(msg);
    }
}

void handle_callbacks(Process *self) {
    // Not reentrant: a callback that waits for a message must not run other callbacks
    if (self->callbacks_blocked > 0)
        return;

    self->callbacks_blocked++;
    handle_signals_callbacks(self);
    self->callbacks_blocked--;
}
