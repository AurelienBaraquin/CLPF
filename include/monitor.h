#pragma once

#include <links.h>
#include <msignal.h>

typedef struct monitor_s {
    links_t *monitors;
    links_t *monitored_by;

    void (*on_process_down)(Process *self, Pid from, unsigned int reason);
} Monitor;

void monitor_init(Monitor *monitor);
void monitor_clear(Monitor *monitor);

void monitor_add(Monitor *monitor, Pid target);
void monitored_by_add(Monitor *monitor, Pid target);

void monitor_remove(Monitor *monitor, Pid target);
void monitored_by_remove(Monitor *monitor, Pid target);
