#include <links.h>
#include <stdlib.h>

void links_init(ProcessLinks *links) {
    links->parent = -1;
    links->children = NULL;
}

void free_link_list(links_t *l) {
    while (l) {
        links_t *next = l->next;
        free(l);
        l = next;
    }
}

void links_clear(ProcessLinks *links) {
    free_link_list(links->children);
}

void add_to_list(links_t **list, Pid pid) {
    for (links_t *l = *list; l; l = l->next) {
        if (l->pid == pid)
            return; // already exists
    }

    links_t *new = malloc(sizeof(links_t));
    if (!new) return;

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

void link_add_child(ProcessLinks *links, Pid child) {
    add_to_list(&links->children, child);
}

int link_has_child(ProcessLinks *links, Pid child) {
    for (links_t *l = links->children; l; l = l->next) {
        if (l->pid == child)
            return 1;
    }
    return 0;
}

void link_remove_child(ProcessLinks *links, Pid child) {
    remove_from_list(&links->children, child);
}

size_t link_count(links_t *list) {
    size_t n = 0;
    for (links_t *l = list; l; l = l->next)
        n++;
    return n;
}

Pid link_get_nth(links_t *list, size_t index) {
    size_t i = 0;
    for (links_t *l = list; l; l = l->next, i++) {
        if (i == index)
            return l->pid;
    }
    return -1;
}
