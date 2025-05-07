#include <api/message.h>
#include <stdio.h>
#include <string.h>

void* receive(Process* self, MessageMatchFn match, Tag userdata) {
    while (1) {
        mailbox_transfer_inbox_to_seen(&self->mailbox);

        Message* msg;

        if (match)
            msg = mailbox_peek_and_match_seen(&self->mailbox, match, userdata);
        else
            msg = mailbox_receive_seen(&self->mailbox);

        if (msg) {
            void *payload = msg->payload;
            if (payload)
                safe_heap_free((safe_heap_t *)msg->heap, msg);
            return payload;
        }

        self->state = PROCESS_WAITING;
        yield(self);
    }
}

void send(Process *self, Pid pid, void* data, size_t size, Tag tag) {
    if (pid == -1) {
        fprintf(stderr, "Invalid PID: %ld\n", pid);
        return;
    }

    Process* other_process = &clpf.process_table[pid];
    if (!other_process) {
        fprintf(stderr, "Process %ld not found\n", pid);
        return;
    }

    if (other_process)
        mailbox_send_message(&other_process->mailbox, (heap_t*)&other_process->heap, data, size, tag);
    else
        mailbox_send_message(&other_process->mailbox, NULL, data, size, tag);

    if (other_process->state == PROCESS_WAITING) {
        other_process->state = PROCESS_READY;
    }

    if (self)
        yield(self);
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

bool MessageMatchFn_StringPrefix(Message *msg, Tag expected) {
    if (!msg || msg->tag.kind != TAG_KIND_STRING || expected.kind != TAG_KIND_STRING)
        return false;
    return strncmp(msg->tag.str_value, expected.str_value, strlen(expected.str_value)) == 0;
}

bool MessageMatchFn_IntGTE(Message *msg, Tag expected) {
    if (!msg || msg->tag.kind != TAG_KIND_INTEGER || expected.kind != TAG_KIND_INTEGER)
        return false;
    return msg->tag.int_value >= expected.int_value;
}

bool MessageMatchFn_IntLTE(Message *msg, Tag expected) {
    if (!msg || msg->tag.kind != TAG_KIND_INTEGER || expected.kind != TAG_KIND_INTEGER)
        return false;
    return msg->tag.int_value <= expected.int_value;
}

bool MessageMatchFn_IntInRange(Message *msg, Tag expected) {
    if (!msg || msg->tag.kind != TAG_KIND_INTEGER || expected.kind != TAG_KIND_INTEGER)
        return false;
    int32_t min = (expected.int_value >> 32) & 0xFFFFFFFF;
    int32_t max = expected.int_value & 0xFFFFFFFF;
    return msg->tag.int_value >= min && msg->tag.int_value <= max;
}

bool MessageMatchFn_ByKind(Message *msg, Tag expected) {
    if (!msg) return false;
    return msg->tag.kind == expected.kind;
}
