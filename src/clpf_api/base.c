#include <api/base.h>
#include <assert.h>
#include <stdlib.h>

void yield(Process *self) {
    assert(self);
    assert(self->scheduler_pid != -1);
    assert(clpf.scheduler_table[self->scheduler_pid].pid == self->scheduler_pid);
    assert(clpf.scheduler_table[self->scheduler_pid].scheduler_acoro);
    aco_yield();

    handle_callbacks(self);
}

void exiting(Process *self, exit_reason reason) {
    // Detach the watchers under the lock, notify them without holding it
    pthread_mutex_lock(&self->lock);
    links_t *watchers = self->monitor.monitored_by;
    self->monitor.monitored_by = NULL;
    self->exit_notified = true;
    pthread_mutex_unlock(&self->lock);

    for (links_t *l = watchers; l; l = l->next) {
        signal_send(l->pid, self->pid, SIGNAL_DOWN, reason);
    }
    free_link_list(watchers);

    self->state = PROCESS_EXITING;
    aco_exit();
    // The scheduler never resumes a finished coroutine
    abort();
}
