#pragma once

#include <utils/pid.h>

typedef struct process_s Process;

typedef struct links_s {
    Pid other_pid; // if other pid = 0, this is a link to the main core
    enum { LINK_TYPE_TO_PARENT, LINK_TYPE_TO_CHILD } type;
    struct links_s *next;
} links_t;

void link_add(Pid self, Pid other_pid, int type);
void link_remove(Pid self, Pid other_pid);
void link_notify_all(Pid self); // lors de la mort

void link_clear(links_t *link);
