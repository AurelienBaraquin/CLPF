#include <api/base.h>
#include <api/monitor.h>
#include <api/link.h>
#include <assert.h>
#include <stdlib.h>

void yield(Process *self) {
    assert(self);
    assert(self->scheduler_pid != -1);
    assert(clpf.scheduler_table[self->scheduler_pid].pid == self->scheduler_pid);
    assert(clpf.scheduler_table[self->scheduler_pid].scheduler_acoro);
    aco_yield();
    self->reductions = REDUCTION_BUDGET;
}

void clpf_reduce(Process *self, int cost) {
    self->reductions -= cost;
    if (self->reductions <= 0)
        yield(self);
}

void process_exit_signal_locked(Process *target, Pid from, unsigned int reason, bool untrappable) {
    if (target->exit_notified)
        return;

    if (!untrappable && target->trap_exit) {
        ExitInfo info = {from, reason};
        if (mailbox_send_message(&target->mailbox, &target->heap, &info, sizeof info, from, MAKE_EXIT_TAG(from)) == 0)
            process_notify_locked(target);
        return;
    }
    if ((!untrappable && reason == EXIT_REASON_NORMAL) || atomic_load(&target->kill_pending))
        return;

    target->kill_reason = untrappable ? EXIT_REASON_KILLED : reason;
    atomic_store(&target->kill_pending, true);
    process_notify_locked(target);
}

void clpf_signal_exit(Pid pid, unsigned int reason, bool untrappable) {
    Process *target = process_lock(pid);
    if (target) {
        process_exit_signal_locked(target, INVALID_PID, reason, untrappable);
        process_unlock(target);
    }
}

void process_terminate(Process *process, unsigned int reason) {
    // Detach under the lock, notify without holding it: no other slot lock is taken while ours is held
    pthread_mutex_lock(&process->lock);
    links_t *watchers = process->monitor.monitored_by;
    links_t *linked = process->linked;
    process->monitor.monitored_by = NULL;
    process->linked = NULL;
    process->exit_notified = true;
    process->exit_reason = reason;
    pthread_mutex_unlock(&process->lock);

    DownInfo down = {process->pid, reason};
    for (links_t *l = watchers; l; l = l->next)
        clpf_deliver(process->pid, l->pid, &down, sizeof down, MAKE_DOWN_TAG(process->pid));
    free_link_list(watchers);

    for (links_t *l = linked; l; l = l->next) {
        Process *partner = process_lock(l->pid);
        if (partner) {
            remove_from_list(&partner->linked, process->pid);
            process_exit_signal_locked(partner, process->pid, reason, false);
            process_unlock(partner);
        }
    }
    free_link_list(linked);
}

void exiting(Process *self, exit_reason reason) {
    process_terminate(self, reason);

    self->state = PROCESS_EXITING;
    aco_exit();
    // The scheduler never resumes a finished coroutine
    abort();
}
