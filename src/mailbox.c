#include <mailbox.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>

void mailbox_init(Mailbox* mailbox) {
    mailbox->seen_head = NULL;
    mailbox->seen_tail = NULL;
    mailbox->inbox_head = NULL;
    mailbox->inbox_tail = NULL;
    atomic_init(&mailbox->inbox_len, 0);
    pthread_mutex_init(&mailbox->lock, NULL);
}

void mailbox_destroy(Mailbox* mbox) {
    pthread_mutex_lock(&mbox->lock);
    mbox->inbox_head = NULL;
    mbox->inbox_tail = NULL;
    mbox->inbox_len = 0;
    pthread_mutex_unlock(&mbox->lock);

    mbox->seen_head = NULL;
    mbox->seen_tail = NULL;
}

int mailbox_send_message(Mailbox* dest, safe_heap_t *dest_heap, void* data, size_t size, Pid sender, Tag tag) {
    Message* msg = safe_heap_alloc(dest_heap, sizeof(Message));
    if (!msg)
        return -1;

    msg->payload = NULL;
    msg->size = 0;
    if (data && size > 0) {
        msg->payload = safe_heap_alloc(dest_heap, size);
        if (!msg->payload) {
            safe_heap_free(dest_heap, msg);
            return -1;
        }
        memcpy(msg->payload, data, size);
        msg->size = size;
    }

    msg->sender_pid = sender;
    msg->tag = tag;
    msg->next = NULL;

    pthread_mutex_lock(&dest->lock);
    if (dest->inbox_tail)
        dest->inbox_tail->next = msg;
    else
        dest->inbox_head = msg;
    dest->inbox_tail = msg;
    dest->inbox_len++;
    pthread_mutex_unlock(&dest->lock);

    return 0;
}

void mailbox_transfer_inbox_to_seen(Mailbox* mbox) {
    pthread_mutex_lock(&mbox->lock);
    if (mbox->inbox_head) {
        if (mbox->seen_tail)
            mbox->seen_tail->next = mbox->inbox_head;
        else
            mbox->seen_head = mbox->inbox_head;
        mbox->seen_tail = mbox->inbox_tail;

        mbox->inbox_head = NULL;
        mbox->inbox_tail = NULL;
        mbox->inbox_len = 0;
    }
    pthread_mutex_unlock(&mbox->lock);
}

bool mailbox_has_incoming(Mailbox* mbox) {
    return atomic_load(&mbox->inbox_len) > 0;
}

Message* mailbox_receive_seen(Mailbox* mbox) {
    Message* msg = mbox->seen_head;
    if (msg) {
        mbox->seen_head = msg->next;
        if (!mbox->seen_head)
            mbox->seen_tail = NULL;
        msg->next = NULL;
    }
    return msg;
}

bool MessageMatchFn_SameTag(Message *msg, Tag expected);

Message* mailbox_peek_and_match_seen(Mailbox* mbox, Tag userdata) {
    Message* prev = NULL;
    Message* msg = mbox->seen_head;

    while (msg) {
        if (MessageMatchFn_SameTag(msg, userdata)) {
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
