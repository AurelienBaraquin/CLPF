#pragma once

#include <clpf_internal.h>

#define INCLUDE_CLPF_MESSAGE

bool MessageMatchFn_SameTag(Message *msg, void *userdata);

#define RECEIVE(match, userdata) \
    receive(self, match, userdata)

#define SEND(pid, data, size) \
    send(self, pid, data, size, NO_TAG)

#define SEND_TAG(pid, data, size, tag) \
    send(self, pid, data, size, tag)

void* receive(Process* self, MessageMatchFn match, void* userdata)
    __attribute__((warn_unused_result));

void send(Process *self, Pid pid, void* data, size_t size, Tag tag);
