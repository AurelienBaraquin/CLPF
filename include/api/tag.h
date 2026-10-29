#pragma once

#include <clpf_internal.h>

#define REGISTER(name, pid) \
    clpf_request_register(self, name, pid)

#define UNREGISTER(name) \
    clpf_request_unregister(self, name)

#define WHEREIS(name) \
    ({ \
        clpf_request_lookup(self, name); \
        Message *clpf_reply = RECEIVE(CORE_MESSAGE_TYPE_NAME_LOOKUP_TAG); \
        Pid clpf_pid = *(Pid *)clpf_reply->payload; \
        MSG_FREE(clpf_reply); \
        clpf_pid; \
    })

void clpf_request_register(Process *self, const char* name, Pid pid);
void clpf_request_unregister(Process *self, const char* name);
void clpf_request_lookup(Process *self, const char* name);
