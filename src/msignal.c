#include <msignal.h>
#include <clpf_internal.h>

void signal_send(Pid to, Pid from, SignalType type, unsigned int reason) {
    Process *target_process = process_lock(to);
    if (!target_process)
        return;

    Signal *signal = safe_heap_alloc(&target_process->heap, sizeof(Signal));
    if (!signal) {
        process_unlock(target_process);
        write(STDERR_FILENO, "Error: Memory allocation failed\n", 33);
        return;
    }

    signal->type = type;
    signal->from = from;
    signal->reason = reason;

    mpscq_push(&target_process->signal_queue, &signal->node);
    process_unlock(target_process);
}
