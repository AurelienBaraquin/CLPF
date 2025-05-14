#include <api/message.h>

void handle_signals_callbacks(Process *self) {
    Signal *sig = FETCH(MAKE_STR_TAG("scheduler::event::signal"));
    if (!sig)
        return;

    switch (sig->type) {
        case SIGNAL_DOWN:
            if (self->monitor.on_process_down)
                self->monitor.on_process_down(self, sig->from, sig->reason);
            break;
        default:
            break;
    }

    safe_heap_free(&self->heap, sig);
}

void handle_callbacks(Process *self) {
    handle_signals_callbacks(self);
}
