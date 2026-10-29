#include <api/message.h>
#include <timer.h>
#include <stdio.h>
#include <string.h>

static Message *take_matching(Process *self, MessageMatch match, Tag pattern) {
    pthread_mutex_lock(&self->lock);
    mailbox_transfer_inbox_to_seen(&self->mailbox);
    pthread_mutex_unlock(&self->lock);

    return mailbox_take_matching(&self->mailbox, match, pattern);
}

Message *clpf_receive(Process *self, MessageMatch match, Tag pattern) {
    while (1) {
        Message *msg = take_matching(self, match, pattern);
        if (msg)
            return msg;

        self->state = PROCESS_WAITING;
        yield(self);
    }
}

Message *clpf_receive_timeout(Process *self, MessageMatch match, Tag pattern, uint64_t timeout_ms) {
    Message *msg = take_matching(self, match, pattern);
    if (msg || timeout_ms == 0)
        return msg;

    TimerId timer = timer_add_timeout(self->pid, timeout_ms);
    Tag timeout_tag = MAKE_TIMEOUT_TAG(timer);
    while (1) {
        msg = take_matching(self, match, pattern);
        if (msg) {
            // A timer that already fired left its message in the mailbox: drop it
            if (timer_cancel(timer) != 0)
                clpf_msg_free(self, take_matching(self, clpf_match_same, timeout_tag));
            return msg;
        }

        Message *timeout = take_matching(self, clpf_match_same, timeout_tag);
        if (timeout) {
            clpf_msg_free(self, timeout);
            return NULL;
        }

        self->state = PROCESS_WAITING;
        yield(self);
    }
}

static bool match_nothing(const Message *msg, Tag pattern) {
    (void)msg;
    (void)pattern;
    return false;
}

void clpf_sleep(Process *self, uint64_t ms) {
    clpf_receive_timeout(self, match_nothing, NO_TAG, ms);
}

Message *clpf_fetch(Process *self, MessageMatch match, Tag pattern) {
    return take_matching(self, match, pattern);
}

int clpf_deliver(Pid from, Pid pid, void *data, size_t size, Tag tag) {
    // Sending to a process that no longer exists is not an error for the runtime
    Process *other_process = process_lock(pid);
    if (!other_process)
        return -1;

    int delivered = mailbox_send_message(&other_process->mailbox, &other_process->heap, data, size, from, tag);
    if (delivered == 0)
        process_notify_locked(other_process);
    process_unlock(other_process);
    if (delivered != 0) {
        fprintf(stderr, "Failed to deliver a message to process %ld\n", pid);
        return -1;
    }
    return 0;
}

int clpf_send(Process *self, Pid pid, void *data, size_t size, Tag tag) {
    int delivered = clpf_deliver(self ? self->pid : INVALID_PID, pid, data, size, tag);
    if (delivered == 0 && self)
        yield(self);
    return delivered;
}

Message *clpf_request(Process *self, Pid pid, void *data, size_t size, Tag tag) {
    if (clpf_send(self, pid, data, size, tag) != 0)
        return NULL;
    return clpf_receive(self, TM_SAME, tag);
}

void clpf_msg_free(Process *self, Message *msg) {
    safe_heap_free(&self->heap, msg);
}

/*
    Matchers
*/

bool clpf_match_same(const Message *msg, Tag pattern) {
    if (pattern.kind == TAG_KIND_NONE)
        return true;
    if (msg->tag.kind != pattern.kind)
        return false;

    switch (pattern.kind) {
        case TAG_KIND_INTEGER:
        case TAG_KIND_DOWN:
        case TAG_KIND_TIMEOUT:
            return msg->tag.int_value == pattern.int_value;
        case TAG_KIND_STRING:
        case TAG_KIND_NOTIFY:
            return strcmp(msg->tag.str_value, pattern.str_value) == 0;
        case TAG_KIND_INTERNAL:
            return msg->tag.internal_code == pattern.internal_code;
        default:
            return false;
    }
}

bool clpf_match_prefix(const Message *msg, Tag pattern) {
    return msg->tag.kind == TAG_KIND_STRING && pattern.kind == TAG_KIND_STRING
        && strncmp(msg->tag.str_value, pattern.str_value, strlen(pattern.str_value)) == 0;
}

bool clpf_match_gte(const Message *msg, Tag pattern) {
    return msg->tag.kind == TAG_KIND_INTEGER && pattern.kind == TAG_KIND_INTEGER
        && msg->tag.int_value >= pattern.int_value;
}

bool clpf_match_lte(const Message *msg, Tag pattern) {
    return msg->tag.kind == TAG_KIND_INTEGER && pattern.kind == TAG_KIND_INTEGER
        && msg->tag.int_value <= pattern.int_value;
}

bool clpf_match_in_range(const Message *msg, Tag pattern) {
    if (msg->tag.kind != TAG_KIND_INTEGER || pattern.kind != TAG_KIND_INTEGER)
        return false;

    // See MAKE_RANGE_TAG
    int32_t min = (int32_t)((uint64_t)pattern.int_value >> 32);
    int32_t max = (int32_t)((uint64_t)pattern.int_value & 0xffffffffu);
    return msg->tag.int_value >= min && msg->tag.int_value <= max;
}

bool clpf_match_kind(const Message *msg, Tag pattern) {
    return msg->tag.kind == pattern.kind;
}
