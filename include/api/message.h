#pragma once

#include <clpf_internal.h>

#define RECEIVE(match, userdata) \
    receive(self, match, userdata)

#define SEND(pid, data, size) \
    send(self, pid, data, size)

void* receive(Process* self, MessageMatchFn match, void* userdata)
    __attribute__((warn_unused_result));

void send(Process* self, Pid pid, void* data, size_t size);
