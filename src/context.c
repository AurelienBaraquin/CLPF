#include <process.h>

void context_entry(Process* self, ucontext_t* arg) {
    if (!self || !arg)
        return;
    self->task();
    setcontext(arg);
}
