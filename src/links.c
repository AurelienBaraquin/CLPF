#include <links.h>
#include <stdlib.h>

void links_init(ProcessLinks *links) {
    links->parent = -1;
    links->children = NULL;
    links->child_count = 0;
}

void free_link_list(links_t *l) {
    while (l) {
        links_t *next = l->next;
        free(l);
        l = next;
    }
}

// The children that are still alive keep stale nodes, never read: their exit
// finds the parent gone and leaves the list alone
void links_clear(ProcessLinks *links) {
    links->children = NULL;
    links->child_count = 0;
}

void add_to_list(links_t **list, Pid pid) {
    for (links_t *l = *list; l; l = l->next) {
        if (l->pid == pid)
            return; // already exists
    }

    links_t *new = malloc(sizeof(links_t));
    if (!new)
        return;

    new->pid = pid;
    new->next = *list;
    *list = new;
}

void remove_from_list(links_t **list, Pid pid) {
    links_t *prev = NULL, *cur = *list;
    while (cur) {
        if (cur->pid == pid) {
            if (prev)
                prev->next = cur->next;
            else
                *list = cur->next;
            free(cur);
            return;
        }
        prev = cur;
        cur = cur->next;
    }
}

void link_add_child(ProcessLinks *links, ProcessLinks *child, Pid child_pid) {
    ChildNode *node = &child->node;
    node->pid = child_pid;
    node->prev = NULL;
    node->next = links->children;
    if (node->next)
        node->next->prev = node;
    links->children = node;
    links->child_count++;
}

void link_remove_child(ProcessLinks *links, ProcessLinks *child) {
    ChildNode *node = &child->node;
    if (node->prev)
        node->prev->next = node->next;
    else
        links->children = node->next;
    if (node->next)
        node->next->prev = node->prev;
    links->child_count--;
}

size_t link_count(links_t *list) {
    size_t n = 0;
    for (links_t *l = list; l; l = l->next)
        n++;
    return n;
}

Pid link_get_nth_child(const ProcessLinks *links, size_t index) {
    size_t i = 0;
    for (const ChildNode *node = links->children; node; node = node->next, i++) {
        if (i == index)
            return node->pid;
    }
    return -1;
}

bool list_contains(links_t *list, Pid pid) {
    for (links_t *l = list; l; l = l->next) {
        if (l->pid == pid)
            return true;
    }
    return false;
}
