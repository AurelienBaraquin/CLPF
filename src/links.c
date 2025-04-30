#include <links.h>
#include <process.h>

void link_add(Process *self, Pid other_pid)
{
    if (!self)
        return;

    links_t *link = malloc(sizeof(links_t));
    if (!link)
        return;

    link->other_pid = other_pid;
    link->type = LINK_TYPE_TO_CHILD;
    link->next = self->links;
    self->links = link;
}

void link_remove(Process *self, Pid other_pid)
{
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

void link_notify_all(Process *self)
{
    // todo but we will see
}

void link_clear(links_t *link)
{
    if (!link)
        return;

    links_t *next = link->next;
    free(link);
    link_clear(next);
}
