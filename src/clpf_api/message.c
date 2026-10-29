#include <api/message.h>
#include <stdio.h>
#include <string.h>
#include <assert.h>

Message* clpf_receive_internal_fetch(Process *self, Tag userdata) {
    Message* msg;

    pthread_mutex_lock(&self->lock);
    mailbox_transfer_inbox_to_seen(&self->mailbox);
    pthread_mutex_unlock(&self->lock);

    if (userdata.kind != TAG_KIND_NONE)
        msg = mailbox_peek_and_match_seen(&self->mailbox, userdata);
    else {
        msg = mailbox_receive_seen(&self->mailbox);
    }

    return msg;
}

Message* clpf_receive_internal(Process *self, Tag userdata) {
    Message* msg;

    while (1) {
        handle_callbacks(self);
        msg = clpf_receive_internal_fetch(self, userdata);

        if (msg)
            return msg;

        self->state = PROCESS_WAITING;
        yield(self);
    }
}

void* receive(Process* self, Tag userdata) {
    Message *msg = clpf_receive_internal(self, userdata);
    assert(msg);

    void *payload = msg->payload;
    safe_heap_free(&self->heap, msg);

    return payload;
}

void *fetch(Process* self, Tag userdata) {
    handle_callbacks(self);
    Message *msg = clpf_receive_internal_fetch(self, userdata);
    if (!msg)
        return NULL;

    void *payload = msg->payload;
    safe_heap_free(&self->heap, msg);

    return payload;
}

int send(Process *self, Pid pid, void* data, size_t size, Tag tag) {
    // Sending to a process that no longer exists is not an error for the runtime
    Process* other_process = process_lock(pid);
    if (!other_process)
        return -1;

    Pid sender_pid = self ? self->pid : -1;

    int delivered = mailbox_send_message(&other_process->mailbox, &other_process->heap, data, size, sender_pid, tag);
    if (delivered == 0)
        process_notify_locked(other_process);
    process_unlock(other_process);
    if (delivered != 0) {
        fprintf(stderr, "Failed to deliver a message to process %ld\n", pid);
        return -1;
    }

    if (self)
        yield(self);

    return 0;
}

void* request(Process *self, Pid pid, void* data, size_t size, Tag tag) {
    if (send(self, pid, data, size, tag) != -1) {
        return receive(self, tag);
    } else {
        return NULL;
    }
}



/*
    Some matching functions for the messages
*/

bool MessageMatchFn_SameTag(Message *msg, Tag expected) {
    if (!msg) return false;

    if (msg->tag.kind != expected.kind) return false;

    switch (msg->tag.kind) {
        case TAG_KIND_INTEGER:
            return msg->tag.int_value == expected.int_value;
        case TAG_KIND_STRING:
            return strcmp(msg->tag.str_value, expected.str_value) == 0;
        case TAG_KIND_INTERNAL:
            return msg->tag.internal_code == expected.internal_code;
        case TAG_KIND_NONE:
            return true;
        default:
            return false;
    }
}
