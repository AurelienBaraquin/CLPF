#include <msignal.h>
#include <clpf_internal.h>

void signal_send(Process *self, Pid to, SignalType type, unsigned int reason) {
    Process *target_process = &clpf.process_table[to];
    if (target_process->pid == -1) {
        write(STDERR_FILENO, "Error: Target process not found\n", 32);
        return;
    }

    Signal *signal = safe_heap_alloc(&target_process->heap, sizeof(Signal));
    if (!signal) {
        write(STDERR_FILENO, "Error: Memory allocation failed\n", 33);
        return;
    }

    signal->type = type;
    signal->from = self->pid;
    signal->reason = reason;

    mpscq_push(&target_process->signal_queue, signal);
}

MPSCQNode *signal_drain(Process *self) {
    return mpscq_drain(&self->signal_queue);
}
