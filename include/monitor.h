#pragma once

#include <links.h>
#include <msignal.h>

typedef struct process_s Process;
typedef void (*ProcessDownCallback)(Process *self, Pid from, unsigned int reason, void *arg);

typedef struct monitor_s {
    links_t *monitors;
    links_t *monitored_by;

    ProcessDownCallback on_process_down;
    void *arg;
} Monitor;

void monitor_init(Monitor *monitor);
void monitor_clear(Monitor *monitor);

void monitor_add(Monitor *monitor, Pid target);
void monitored_by_add(Monitor *monitor, Pid target);

void monitor_remove(Monitor *monitor, Pid target);
void monitored_by_remove(Monitor *monitor, Pid target);

void monitor_run(Monitor *monitor, Process *self, Pid from, unsigned int reason);
