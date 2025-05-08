#pragma once

#include <clpf_internal.h>

#define REGISTER(name, pid) \
    __clpf_request_register(self, name, pid)

#define UNREGISTER(name) \
    __clpf_request_unregister(self, name)

#define WHEREIS(name) \
    ({ \
        __clpf_request_lookup(self, name); \
        Pid *__pid_ptr = (Pid*)RECEIVE(CORE_MESSAGE_TYPE_NAME_LOOKUP_TAG); \
        Pid __pid = *__pid_ptr; \
        safe_heap_free(&self->heap, __pid_ptr); \
        __pid; \
    })

void __clpf_request_register(Process *self, const char* name, Pid pid);
void __clpf_request_unregister(Process *self, const char* name);
void __clpf_request_lookup(Process *self, const char* name);
