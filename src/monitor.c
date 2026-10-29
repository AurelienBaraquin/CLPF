#include <monitor.h>

void monitor_init(Monitor *monitor) {
    monitor->monitored_by = NULL;
}

void monitor_clear(Monitor *monitor) {
    free_link_list(monitor->monitored_by);
    monitor->monitored_by = NULL;
}

void monitored_by_add(Monitor *monitor, Pid watcher) {
    if (watcher != INVALID_PID)
        add_to_list(&monitor->monitored_by, watcher);
}

void monitored_by_remove(Monitor *monitor, Pid watcher) {
    if (watcher != INVALID_PID)
        remove_from_list(&monitor->monitored_by, watcher);
}
