#pragma once

#include <links.h>

// The processes to notify with a DOWN message when this one exits
typedef struct monitor_s {
    links_t *monitored_by;
} Monitor;

void monitor_init(Monitor *monitor);
void monitor_clear(Monitor *monitor);

void monitored_by_add(Monitor *monitor, Pid watcher);
void monitored_by_remove(Monitor *monitor, Pid watcher);
