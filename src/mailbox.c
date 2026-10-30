#include <mailbox.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>

void mailbox_init(Mailbox *mailbox) {
    mailbox->seen_head = NULL;
    mailbox->seen_tail = NULL;
    mailbox->inbox_head = NULL;
    mailbox->inbox_tail = NULL;
}

void mailbox_destroy(Mailbox *mbox) {
    mbox->inbox_head = NULL;
    mbox->inbox_tail = NULL;

    mbox->seen_head = NULL;
    mbox->seen_tail = NULL;
}

// The payload starts at the first 16-byte boundary after the header
#define MESSAGE_HEADER_SIZE ((sizeof(Message) + 15) & ~(size_t)15)

static bool tag_has_string(Tag tag) {
    return tag.kind == TAG_KIND_STRING || tag.kind == TAG_KIND_NOTIFY;
}

static void enqueue(Mailbox *dest, Message *msg) {
    msg->next = NULL;
    if (dest->inbox_tail)
        dest->inbox_tail->next = msg;
    else
        dest->inbox_head = msg;
    dest->inbox_tail = msg;
}

int mailbox_send_message(Mailbox *dest, safe_heap_t *dest_heap, void *data, size_t size, Pid sender, Tag tag) {
    if (!data)
        size = 0;
    size_t string_size = tag_has_string(tag) ? strlen(tag.str_value) + 1 : 0;

    Message *msg = safe_heap_alloc(dest_heap, MESSAGE_HEADER_SIZE + size + string_size);
    if (!msg)
        return -1;

    msg->payload = size > 0 ? (char *)msg + MESSAGE_HEADER_SIZE : NULL;
    msg->size = size;
    if (size > 0)
        memcpy(msg->payload, data, size);

    msg->tag = tag;
    if (string_size > 0) {
        char *string = (char *)msg + MESSAGE_HEADER_SIZE + size;
        memcpy(string, tag.str_value, string_size);
        msg->tag.str_value = string;
    }

    msg->sender_pid = sender;
    msg->bin = NULL;
    enqueue(dest, msg);
    return 0;
}

// A binary message: the header, the reference it holds, then the tag string if any
#define BIN_HOLD_SIZE ((sizeof(BinHold) + 15) & ~(size_t)15)

BinHold *mailbox_bin_hold(Message *msg) {
    return (BinHold *)((char *)msg + MESSAGE_HEADER_SIZE);
}

int mailbox_send_bin(Mailbox *dest, safe_heap_t *dest_heap, BinHold **holds, Bin *bin, Pid sender, Tag tag) {
    size_t string_size = tag_has_string(tag) ? strlen(tag.str_value) + 1 : 0;

    Message *msg = safe_heap_alloc(dest_heap, MESSAGE_HEADER_SIZE + BIN_HOLD_SIZE + string_size);
    if (!msg)
        return -1;

    msg->payload = bin_data(bin);
    msg->size = bin_size(bin);
    msg->tag = tag;
    if (string_size > 0) {
        char *string = (char *)msg + MESSAGE_HEADER_SIZE + BIN_HOLD_SIZE;
        memcpy(string, tag.str_value, string_size);
        msg->tag.str_value = string;
    }
    msg->sender_pid = sender;
    msg->bin = bin;

    BinHold *hold = mailbox_bin_hold(msg);
    hold->bin = bin;
    hold->owned = false;
    bin_retain(bin);
    bin_hold_link(holds, hold);

    enqueue(dest, msg);
    return 0;
}

void mailbox_transfer_inbox_to_seen(Mailbox *mbox) {
    if (mbox->inbox_head) {
        if (mbox->seen_tail)
            mbox->seen_tail->next = mbox->inbox_head;
        else
            mbox->seen_head = mbox->inbox_head;
        mbox->seen_tail = mbox->inbox_tail;

        mbox->inbox_head = NULL;
        mbox->inbox_tail = NULL;
    }
}

Message *mailbox_take_matching(Mailbox *mbox, MessageMatch match, Tag pattern) {
    Message *prev = NULL;
    Message *msg = mbox->seen_head;

    while (msg) {
        if (match(msg, pattern)) {
            if (prev)
                prev->next = msg->next;
            else
                mbox->seen_head = msg->next;
            if (msg == mbox->seen_tail)
                mbox->seen_tail = prev;
            msg->next = NULL;
            return msg;
        }
        prev = msg;
        msg = msg->next;
    }
    return NULL;
}
