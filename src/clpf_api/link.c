#include <api/link.h>

// Locks self and the process pid in table order, so that two processes
// linking each other at the same time cannot deadlock. Returns the other
// process, or NULL (nothing locked) if it does not exist.
static Process *lock_pair(Process *self, Pid pid) {
    if (PID_INDEX(pid) < PID_INDEX(self->pid)) {
        Process *other = process_lock(pid);
        if (other)
            pthread_mutex_lock(&self->lock);
        return other;
    }

    pthread_mutex_lock(&self->lock);
    Process *other = process_lock(pid);
    if (!other)
        pthread_mutex_unlock(&self->lock);
    return other;
}

static void unlock_pair(Process *self, Process *other) {
    pthread_mutex_unlock(&self->lock);
    process_unlock(other);
}

int clpf_link(Process *self, Pid pid) {
    if (pid == self->pid)
        return -1;

    Process *other = lock_pair(self, pid);
    if (!other)
        return -1;

    int result = -1;
    if (!other->exit_notified) {
        add_to_list(&self->linked, pid);
        add_to_list(&other->linked, self->pid);
        result = 0;
    }
    unlock_pair(self, other);
    return result;
}

int clpf_unlink(Process *self, Pid pid) {
    Process *other = lock_pair(self, pid);
    if (!other) {
        // The partner is gone: only our side may remain
        pthread_mutex_lock(&self->lock);
        bool was_linked = list_contains(self->linked, pid);
        remove_from_list(&self->linked, pid);
        pthread_mutex_unlock(&self->lock);
        return was_linked ? 0 : -1;
    }

    bool was_linked = list_contains(self->linked, pid);
    remove_from_list(&self->linked, pid);
    remove_from_list(&other->linked, self->pid);
    unlock_pair(self, other);
    return was_linked ? 0 : -1;
}

void clpf_trap_exit(Process *self, bool enabled) {
    pthread_mutex_lock(&self->lock);
    self->trap_exit = enabled;
    pthread_mutex_unlock(&self->lock);
}

int clpf_kill(Process *self, Pid pid) {
    if (pid == self->pid)
        exiting(self, EXIT_REASON_KILLED);

    Process *target = process_lock(pid);
    if (!target)
        return -1;

    int result = -1;
    if (!target->exit_notified && !atomic_load(&target->kill_pending)) {
        process_exit_signal_locked(target, self->pid, EXIT_REASON_KILLED, true);
        result = 0;
    }
    process_unlock(target);
    return result;
}

int clpf_send_exit(Process *self, Pid pid, unsigned int reason) {
    if (pid == self->pid) {
        pthread_mutex_lock(&self->lock);
        bool trapping = self->trap_exit;
        pthread_mutex_unlock(&self->lock);
        if (!trapping && reason != EXIT_REASON_NORMAL)
            exiting(self, reason);
    }

    Process *target = process_lock(pid);
    if (!target)
        return -1;
    process_exit_signal_locked(target, self->pid, reason, false);
    process_unlock(target);
    return 0;
}
