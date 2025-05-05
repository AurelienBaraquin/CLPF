#include <process.h>

void context_entry(Process* self, ucontext_t* arg) {
    if (!self || !arg)
        return;
    funcall_call(&self->taskcall);
    setcontext(arg);
}
