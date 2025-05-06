#pragma once

#include <clpf_internal.h>

#define INCLUDE_CLPF_MESSAGE

bool MessageMatchFn_SameTag(Message *msg, void *userdata);
bool MessageMatchFn_SameDataString(Message *msg, void *userdata);


#define RECEIVE(match, userdata) \
    receive(self, match, userdata)

#define SEND(pid, data, size) \
    send(self, pid, data, size, NO_TAG)

#define SEND_TAG(pid, data, size, tag) \
    send(self, pid, data, size, tag)


// ONLY STATIC STRING ARE ALLOWED DO NOT PUT DYNAMIC STRING !!!!!
#define NOTIFY(pid, data) \
    send(self, pid, (void *)data, sizeof(data), NO_TAG)

#define WAIT_NOTIF(data) \
    receive(self, MessageMatchFn_SameDataString, (void *)data)



void* receive(Process* self, MessageMatchFn match, void* userdata);
void send(Process *self, Pid pid, void* data, size_t size, Tag tag);
