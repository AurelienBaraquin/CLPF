#include <monitor.h>

void monitor_init(Monitor *monitor) {
    if (!monitor)
        return;

    monitor->monitors = NULL;
    monitor->monitored_by = NULL;
    monitor->on_process_down = NULL;
}

void monitor_clear(Monitor *monitor) {
    if (!monitor)
        return;

    free_link_list(monitor->monitors);
    free_link_list(monitor->monitored_by);
    monitor->on_process_down = NULL;
}

void monitor_add(Monitor *monitor, Pid target) {
    if (!monitor || target == -1)
        return;

    add_to_list(&monitor->monitors, target);
}

void monitored_by_add(Monitor *monitor, Pid target) {
    if (!monitor || target == -1)
        return;

    add_to_list(&monitor->monitored_by, target);
}

void monitor_remove(Monitor *monitor, Pid target) {
    if (!monitor || target == -1)
        return;

    remove_from_list(&monitor->monitors, target);
}

void monitored_by_remove(Monitor *monitor, Pid target) {
    if (!monitor || target == -1)
        return;

    remove_from_list(&monitor->monitored_by, target);
}
