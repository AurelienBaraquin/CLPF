#pragma once

#include <utils/pid.h>
#include <stddef.h>

typedef struct links_s {
    Pid pid;
    struct links_s *next;
} links_t;

typedef struct {
    Pid parent;
    links_t *children;
    links_t *monitors;
    links_t *monitored_by;
} ProcessLinks;

void links_init(ProcessLinks *links);
void links_clear(ProcessLinks *links);

void link_add_child(ProcessLinks *links, Pid child);
void link_add_monitor(ProcessLinks *links, Pid monitor);
void link_add_monitored_by(ProcessLinks *links, Pid target);

int link_has_child(ProcessLinks *links, Pid child);
void link_remove_child(ProcessLinks *links, Pid child);

size_t link_count(links_t *list);
Pid link_get_nth(links_t *list, size_t index);
