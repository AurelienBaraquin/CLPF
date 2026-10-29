#include <api/monitor.h>

void process_monitor(Process *self, Pid target) {
    Process *process = process_lock(target);
    if (!process)
        return;

    monitored_by_add(&process->monitor, self->pid);
    process_unlock(process);

    monitor_add(&self->monitor, target);
}

void process_demonitor(Process *self, Pid target) {
    Process *process = process_lock(target);
    if (process) {
        monitored_by_remove(&process->monitor, self->pid);
        process_unlock(process);
    }

    monitor_remove(&self->monitor, target);
}
