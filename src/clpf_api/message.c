#include <api/message.h>
#include <stdio.h>
#include <string.h>
#include <assert.h>

Message* __receive_internal_fetch(Process *self, Tag userdata) {
    Message* msg;

    mailbox_transfer_inbox_to_seen(&self->mailbox);

    if (userdata.kind != TAG_KIND_NONE)
        msg = mailbox_peek_and_match_seen(&self->mailbox, userdata);
    else {
        msg = mailbox_receive_seen(&self->mailbox);
    }

    return msg;
}

Message* __receive_internal(Process *self, Tag userdata) {
    Message* msg;

    while (1) {
        msg = __receive_internal_fetch(self, userdata);

        if (msg)
            return msg;

        self->state = PROCESS_WAITING;
        yield(self);
    }
}

void* receive(Process* self, Tag userdata) {
    Message *msg = __receive_internal(self, userdata);
    assert(msg);

    void *payload = msg->payload;
    safe_heap_free(&self->heap, msg);

    return payload;
}

void *fetch(Process* self, Tag userdata) {
    Message *msg = __receive_internal_fetch(self, userdata);
    if (!msg)
        return NULL;

    void *payload = msg->payload;
    safe_heap_free(&self->heap, msg);

    return payload;
}

int send(Process *self, Pid pid, void* data, size_t size, Tag tag) {
    if (pid == -1) {
        fprintf(stderr, "Invalid PID: %ld\n", pid);
        return -1;
    }

    Process* other_process = &clpf.process_table[pid];
    if (!other_process) {
        fprintf(stderr, "Process %ld not found\n", pid);
        return -1;
    }

    Pid sender_pid = self ? self->pid : -1;

    if (mailbox_send_message(&other_process->mailbox, &other_process->heap, data, size, sender_pid, tag) != 0) {
        fprintf(stderr, "Failed to deliver a message to process %ld\n", pid);
        return -1;
    }

    if (other_process->state == PROCESS_WAITING) {
        other_process->state = PROCESS_READY;
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
