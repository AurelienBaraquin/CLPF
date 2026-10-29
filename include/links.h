#pragma once

#include <utils/pid.h>
#include <stdbool.h>
#include <stddef.h>

typedef struct links_s {
    Pid pid;
    struct links_s *next;
} links_t;

typedef struct {
    Pid parent;
    links_t *children;
} ProcessLinks;

// Utils
void free_link_list(links_t *l);
void add_to_list(links_t **list, Pid pid);
void remove_from_list(links_t **list, Pid pid);
bool list_contains(links_t *list, Pid pid);

void links_init(ProcessLinks *links);
void links_clear(ProcessLinks *links);

void link_add_child(ProcessLinks *links, Pid child);

void link_remove_child(ProcessLinks *links, Pid child);

size_t link_count(links_t *list);
Pid link_get_nth(links_t *list, size_t index);
