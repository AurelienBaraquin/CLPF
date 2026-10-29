#include <clpf_internal.h>
#include <clpf_const.h>
#include <stdio.h>
#include <api/message.h>
#include <string.h>

void __clpf_request_register(Process *self, const char* name, Pid pid)
{
    CoreMessage* msg = malloc(sizeof(CoreMessage));
    if (!msg) {
        fprintf(stderr, "Failed to allocate memory for message\n");
        return;
    }

    msg->op = CORE_MESSAGE_TYPE_NAME_REGISTER;
    msg->sender = self->pid;
    msg->reply_to = -1;
    msg->data.name_register.pid = pid;
    strncpy(msg->data.name_register.name, name, sizeof(msg->data.name_register.name) - 1);
    msg->data.name_register.name[sizeof(msg->data.name_register.name) - 1] = '\0';

    clpf_post(msg);
}

void __clpf_request_unregister(Process *self, const char* name)
{
    CoreMessage* msg = malloc(sizeof(CoreMessage));
    if (!msg) {
        fprintf(stderr, "Failed to allocate memory for message\n");
        return;
    }

    msg->op = CORE_MESSAGE_TYPE_NAME_UNREGISTER;
    msg->sender = self->pid;
    msg->reply_to = -1;
    strncpy(msg->data.name_unregister.name, name, sizeof(msg->data.name_unregister.name) - 1);
    msg->data.name_unregister.name[sizeof(msg->data.name_unregister.name) - 1] = '\0';

    clpf_post(msg);
}

void __clpf_request_lookup(Process *self, const char* name)
{
    CoreMessage* msg = malloc(sizeof(CoreMessage));
    if (!msg) {
        fprintf(stderr, "Failed to allocate memory for message\n");
        return;
    }

    msg->op = CORE_MESSAGE_TYPE_NAME_LOOKUP;
    msg->sender = self->pid;
    msg->reply_to = self->pid;
    strncpy(msg->data.name_lookup.name, name, sizeof(msg->data.name_lookup.name) - 1);
    msg->data.name_lookup.name[sizeof(msg->data.name_lookup.name) - 1] = '\0';

    clpf_post(msg);
}
