#include <links.h>
#include <process.h>
#include <clpf_internal.h>
#include <stdio.h>

void link_add(Pid self_pid, Pid other_pid, int type)
{
    if (type != LINK_TYPE_TO_PARENT && type != LINK_TYPE_TO_CHILD)
        return;

    Process *self = &clpf.process_table[self_pid];

    if (!self) {
        fprintf(stderr, "Process %ld not found\n", self_pid);
        return;
    }

    links_t *link = malloc(sizeof(links_t));
    if (!link)
        return;

    link->other_pid = other_pid;
    link->type = type;
    link->next = self->links;
    self->links = link;
}

void link_remove(Pid self_pid, Pid other_pid)
{
    Process *self = &clpf.process_table[self_pid];

    if (!self)
        return;

    links_t *prev = NULL;
    links_t *link = self->links;

    while (link) {
        if (link->other_pid == other_pid) {
            if (prev) {
                prev->next = link->next;
            } else {
                self->links = link->next;
            }
            free(link);
            return;
        }
        prev = link;
        link = link->next;
    }
}

void link_notify_all(Pid self)
{
    // todo but we will see
}

void link_clear(links_t *link)
{
    if (!link)
        return;

    for (links_t *l = link; l != NULL;) {
        links_t *next = l->next;
        free(l);
        l = next;
    }
}
