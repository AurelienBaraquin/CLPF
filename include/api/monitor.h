#pragma once

#include <clpf_internal.h>

#define MONITOR_INIT(callback) \
    self->monitor.on_process_down = callback;

#define MONITOR_ADD(target) \
    monitor_add(&self->monitor, target); \
    monitored_by_add(&clpf.process_table[target].monitor, self->pid);

#define MONITOR_REMOVE(target) \
    monitor_remove(&self->monitor, target); \
    monitored_by_remove(&clpf.process_table[target].monitor, self->pid);
