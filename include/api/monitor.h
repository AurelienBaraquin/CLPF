#pragma once

#include <clpf_internal.h>

#define MONITOR_INIT(callback, _arg) \
    self->monitor.on_process_down = (ProcessDownCallback)callback; \
    self->monitor.arg = _arg;

#define MONITOR_ADD(target) \
    process_monitor(self, target)

#define MONITOR_REMOVE(target) \
    process_demonitor(self, target)

// Ask to receive a DOWN signal when target exits (no-op if target is already gone)
void process_monitor(Process *self, Pid target);
void process_demonitor(Process *self, Pid target);
