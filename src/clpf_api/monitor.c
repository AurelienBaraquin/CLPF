#include <api/monitor.h>
#include <api/base.h>

void process_monitor(Process *self, Pid target) {
    monitor_add(&self->monitor, target);

    Process *process = process_lock(target);
    if (process && !process->exit_notified) {
        monitored_by_add(&process->monitor, self->pid);
        process_unlock(process);
        return;
    }
    if (process)
        process_unlock(process);

    // Too late to watch it exit: report it gone right away
    signal_send(self->pid, target, SIGNAL_DOWN, EXIT_REASON_NOPROC);
}

void process_demonitor(Process *self, Pid target) {
    Process *process = process_lock(target);
    if (process) {
        monitored_by_remove(&process->monitor, self->pid);
        process_unlock(process);
    }

    monitor_remove(&self->monitor, target);
}
