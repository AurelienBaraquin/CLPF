#include <api/message.h>
#include <timer.h>
#include <stdio.h>
#include <string.h>

// cursor: the last message this RECEIVE already looked at, so that each wakeup
// only looks at the messages that arrived since (NULL: from the start)
static Message *take_matching(Process *self, Message **cursor, MessageMatch match, Tag pattern) {
    pthread_mutex_lock(&self->lock);
    mailbox_transfer_inbox_to_seen(&self->mailbox);
    pthread_mutex_unlock(&self->lock);

    Message *msg = mailbox_take_matching_after(&self->mailbox, cursor, match, pattern);
    if (msg)
        atomic_fetch_sub(&self->queued, 1);
    return msg;
}

static Message *take_first(Process *self, MessageMatch match, Tag pattern) {
    Message *cursor = NULL;
    return take_matching(self, &cursor, match, pattern);
}

// Waits until a message that may match arrives. Returns at once if messages
// arrived since the last look: they have to be looked at first.
static void wait_for(Process *self, MessageMatch match, Tag pattern) {
    pthread_mutex_lock(&self->lock);
    if (self->mailbox.inbox_head) {
        pthread_mutex_unlock(&self->lock);
        // A flood of messages it does not want must not keep the scheduler forever
        clpf_reduce(self, REDUCTIONS_FETCH);
        return;
    }
    self->wait_match = match;
    self->wait_pattern = pattern;
    self->state = PROCESS_WAITING;
    pthread_mutex_unlock(&self->lock);

    yield(self);

    pthread_mutex_lock(&self->lock);
    self->wait_match = NULL;
    pthread_mutex_unlock(&self->lock);
}

// What the runtime sends always gets in, even into a full mailbox
static bool from_runtime(Pid from, Tag tag) {
    return from == INVALID_PID || tag.kind == TAG_KIND_DOWN || tag.kind == TAG_KIND_EXIT ||
           tag.kind == TAG_KIND_TIMEOUT || tag.kind == TAG_KIND_REPLY;
}

Message *clpf_receive(Process *self, MessageMatch match, Tag pattern) {
    Message *cursor = NULL;
    while (1) {
        Message *msg = take_matching(self, &cursor, match, pattern);
        if (msg)
            return msg;
        wait_for(self, match, pattern);
    }
}

Message *clpf_receive_timeout(Process *self, MessageMatch match, Tag pattern, uint64_t timeout_ms) {
    if (timeout_ms == CLPF_INFINITY)
        return clpf_receive(self, match, pattern);

    Message *cursor = NULL;
    Message *msg = take_matching(self, &cursor, match, pattern);
    if (msg || timeout_ms == 0)
        return msg;

    // The timeout message can only be newer than what was already looked at
    Message *timeout_cursor = cursor;
    TimerId timer = timer_add_timeout(self->pid, timeout_ms);
    Tag timeout_tag = MAKE_TIMEOUT_TAG(timer);
    while (1) {
        msg = take_matching(self, &cursor, match, pattern);
        if (msg) {
            // A timer that already fired left its message in the mailbox: drop it
            if (timer_cancel(timer) != 0)
                clpf_msg_free(self, take_first(self, clpf_match_same, timeout_tag));
            return msg;
        }

        Message *timeout = take_matching(self, &timeout_cursor, clpf_match_same, timeout_tag);
        if (timeout) {
            clpf_msg_free(self, timeout);
            return NULL;
        }

        wait_for(self, match, pattern);
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
    clpf_reduce(self, REDUCTIONS_FETCH);
    return take_first(self, match, pattern);
}

// A waiting receiver only wakes for a message it may take, or one from the runtime
// (timeouts, DOWN, exit signals...). The new message is the last of its inbox.
static bool wakes_receiver(Process *receiver, Pid from, Tag tag) {
    return !receiver->wait_match || from_runtime(from, tag) ||
           receiver->wait_match(receiver->mailbox.inbox_tail, receiver->wait_pattern);
}

// bin: a shared binary to send instead of a copy of data
static int deliver(Pid from, Pid pid, void *data, size_t size, Tag tag, Bin *bin) {
    // Sending to a process that no longer exists is not an error for the runtime
    Process *other_process = process_lock(pid);
    if (!other_process)
        return -1;

    // The answer to a REQUEST_REF that already gave up is dropped, not left in the mailbox
    if (tag.kind == TAG_KIND_REPLY && other_process->awaited_reply != tag.int_value) {
        process_unlock(other_process);
        return 0;
    }

    if (other_process->mailbox_limit > 0 && atomic_load(&other_process->queued) >= other_process->mailbox_limit &&
        !from_runtime(from, tag)) {
        process_unlock(other_process);
        return SEND_FULL;
    }

    int delivered =
        bin ? mailbox_send_bin(&other_process->mailbox, &other_process->heap, &other_process->bin_holds, bin, from, tag)
            : mailbox_send_message(&other_process->mailbox, &other_process->heap, data, size, from, tag);
    if (delivered == 0) {
        atomic_fetch_add(&other_process->queued, 1);
        if (wakes_receiver(other_process, from, tag))
            process_notify_locked(other_process);
    }
    process_unlock(other_process);
    if (delivered != 0) {
        fprintf(stderr, "Failed to deliver a message to process %ld\n", pid);
        return -1;
    }
    return 0;
}

int clpf_deliver(Pid from, Pid pid, void *data, size_t size, Tag tag) {
    return deliver(from, pid, data, size, tag, NULL);
}

int clpf_deliver_bin(Pid from, Pid pid, Bin *bin, Tag tag) {
    return deliver(from, pid, NULL, 0, tag, bin);
}

int clpf_send(Process *self, Pid pid, void *data, size_t size, Tag tag) {
    int delivered = clpf_deliver(self ? self->pid : INVALID_PID, pid, data, size, tag);
    if (self)
        clpf_reduce(self, REDUCTIONS_SEND);
    return delivered;
}

Message *clpf_request(Process *self, Pid pid, void *data, size_t size, Tag tag) {
    return clpf_request_timeout(self, pid, data, size, tag, CLPF_INFINITY);
}

Message *clpf_request_timeout(Process *self, Pid pid, void *data, size_t size, Tag tag, uint64_t timeout_ms) {
    if (clpf_send(self, pid, data, size, tag) != 0)
        return NULL;
    return clpf_receive_timeout(self, TM_SAME, tag, timeout_ms);
}

static _Atomic Ref last_ref;

Ref clpf_make_ref(void) {
    return atomic_fetch_add(&last_ref, 1) + 1;
}

static void await_reply(Process *self, Ref ref) {
    pthread_mutex_lock(&self->lock);
    self->awaited_reply = (int64_t)ref;
    pthread_mutex_unlock(&self->lock);
}

Message *clpf_request_ref(Process *self, Pid pid, void *data, size_t size, uint64_t timeout_ms) {
    Ref ref = clpf_make_ref();

    // Expect the reply before sending: it may come back before SEND returns
    await_reply(self, ref);
    Message *reply = NULL;
    if (clpf_send(self, pid, data, size, MAKE_REF_TAG(ref)) == 0)
        reply = clpf_receive_timeout(self, TM_SAME, MAKE_REPLY_TAG(ref), timeout_ms);
    await_reply(self, 0);
    return reply;
}

int clpf_reply(Process *self, const Message *msg, void *data, size_t size) {
    Tag tag = msg->tag.kind == TAG_KIND_REF ? MAKE_REPLY_TAG(msg->tag.int_value) : msg->tag;
    return clpf_send(self, msg->sender_pid, data, size, tag);
}

void clpf_msg_free(Process *self, Message *msg) {
    if (msg && msg->bin) {
        // Senders link new references to the same list: unlink under the lock
        pthread_mutex_lock(&self->lock);
        bin_hold_unlink(&self->bin_holds, mailbox_bin_hold(msg));
        pthread_mutex_unlock(&self->lock);
        bin_release(msg->bin);
    }
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
        case TAG_KIND_EXIT:
        case TAG_KIND_TIMEOUT:
        case TAG_KIND_REF:
        case TAG_KIND_REPLY:
        case TAG_KIND_FD: return msg->tag.int_value == pattern.int_value;
        case TAG_KIND_STRING:
        case TAG_KIND_NOTIFY: return strcmp(msg->tag.str_value, pattern.str_value) == 0;
        case TAG_KIND_INTERNAL: return msg->tag.internal_code == pattern.internal_code;
        default: return false;
    }
}

bool clpf_match_prefix(const Message *msg, Tag pattern) {
    return msg->tag.kind == TAG_KIND_STRING && pattern.kind == TAG_KIND_STRING &&
           strncmp(msg->tag.str_value, pattern.str_value, strlen(pattern.str_value)) == 0;
}

bool clpf_match_gte(const Message *msg, Tag pattern) {
    return msg->tag.kind == TAG_KIND_INTEGER && pattern.kind == TAG_KIND_INTEGER &&
           msg->tag.int_value >= pattern.int_value;
}

bool clpf_match_lte(const Message *msg, Tag pattern) {
    return msg->tag.kind == TAG_KIND_INTEGER && pattern.kind == TAG_KIND_INTEGER &&
           msg->tag.int_value <= pattern.int_value;
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
