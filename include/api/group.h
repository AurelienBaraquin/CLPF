#pragma once

#include <clpf_internal.h>

// Process groups: named sets of processes, to reach them all with one send.
// A process may belong to several groups; it leaves them all when it ends.
// Group names are any string of at most 63 characters.

// 0, or -1 if the name is invalid or out of memory. Joining twice is joining once.
#define GROUP_JOIN(name) clpf_group_join(self, name)
// 0, or -1 if the caller is not a member
#define GROUP_LEAVE(name) clpf_group_leave(self, name)
// Writes up to max members to buffer, in join order; returns how many there are
#define GROUP_MEMBERS(name, buffer, max) clpf_group_members(name, buffer, max)
// Sends a copy to every member, the caller included if it is one; returns
// how many members got it (a member that just ended or whose mailbox is full does not)
#define GROUP_SEND(name, data, size, tag) clpf_group_send(self, name, data, size, tag)

int clpf_group_join(Process *self, const char *name);
int clpf_group_leave(Process *self, const char *name);
size_t clpf_group_members(const char *name, Pid *buffer, size_t max);
size_t clpf_group_send(Process *self, const char *name, void *data, size_t size, Tag tag);
