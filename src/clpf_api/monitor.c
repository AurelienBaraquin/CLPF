#include <api/monitor.h>
#include <api/base.h>

void process_monitor(Process *self, Pid target) {
    Process *process = process_lock(target);
    if (process && !process->exit_notified) {
        monitored_by_add(&process->monitor, self->pid);
        process_unlock(process);
        return;
    }
    if (process)
        process_unlock(process);

    // Too late to watch it exit: report it gone right away
    DownInfo down = {target, EXIT_REASON_NOPROC};
    clpf_deliver(target, self->pid, &down, sizeof down, MAKE_DOWN_TAG(target));
}

void process_demonitor(Process *self, Pid target) {
    Process *process = process_lock(target);
    if (process) {
        monitored_by_remove(&process->monitor, self->pid);
        process_unlock(process);
    }
}
