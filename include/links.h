#pragma once

#include <utils/pid.h>
#include <stdbool.h>
#include <stddef.h>

typedef struct links_s {
    Pid pid;
    struct links_s *next;
} links_t;

// A process's entry in its parent's children, kept in its own slot so that
// adding or removing a child takes constant time
typedef struct child_node_s {
    Pid pid;
    struct child_node_s *prev;
    struct child_node_s *next;
} ChildNode;

typedef struct {
    Pid parent;
    ChildNode *children; // Newest first
    size_t child_count;
    ChildNode node; // Guarded by the parent's lock: this process in its parent's children
} ProcessLinks;

// Utils
void free_link_list(links_t *l);
void add_to_list(links_t **list, Pid pid);
void remove_from_list(links_t **list, Pid pid);
bool list_contains(links_t *list, Pid pid);

void links_init(ProcessLinks *links);
void links_clear(ProcessLinks *links);

// child must not be in a list yet; it stays in this one until removed or the parent is cleared
void link_add_child(ProcessLinks *links, ProcessLinks *child, Pid child_pid);

// child must be one of the children of links
void link_remove_child(ProcessLinks *links, ProcessLinks *child);

size_t link_count(links_t *list);
Pid link_get_nth_child(const ProcessLinks *links, size_t index);
