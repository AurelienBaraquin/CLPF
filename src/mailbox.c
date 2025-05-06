#include <mailbox.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <memory/heap.h>

void mailbox_init(Mailbox* mailbox) {
    mailbox->seen_head = NULL;
    mailbox->seen_tail = NULL;
    mailbox->inbox_head = NULL;
    mailbox->inbox_tail = NULL;
    mailbox->inbox_len = 0;
    pthread_mutex_init(&mailbox->lock, NULL);
}

void mailbox_destroy(Mailbox* mbox, heap_t *heap) {
    // Inbox : protégée
    pthread_mutex_lock(&mbox->lock);
    Message *msg = mbox->inbox_head;
    while (msg) {
        Message *next = msg->next;
        if (heap) {
            if (msg->payload) heap_free(heap, msg->payload);
            heap_free(heap, msg);
        } else {
            if (msg->payload) free(msg->payload);
            free(msg);
        }
        msg = next;
    }
    pthread_mutex_unlock(&mbox->lock);

    // Seen : non protégé
    msg = mbox->seen_head;
    while (msg) {
        Message *next = msg->next;
        if (heap) {
            if (msg->payload) heap_free(heap, msg->payload);
            heap_free(heap, msg);
        } else {
            if (msg->payload) free(msg->payload);
            free(msg);
        }
        msg = next;
    }
}

void mailbox_send_message(Mailbox* dest, heap_t *dest_heap, void* data, size_t size, Tag tag) {
    Message* msg = dest_heap == NULL ? malloc(sizeof(Message)) : heap_alloc(dest_heap, sizeof(Message));
    if (!msg) return;

    msg->payload = dest_heap == NULL ? malloc(size) : heap_alloc(dest_heap, size);
    if (!msg->payload) { heap_free(dest_heap, msg); return; }

    msg->tag = tag;
    msg->heap = dest_heap;

    if (data)
        memcpy(msg->payload, data, size);
    msg->size = size;
    msg->next = NULL;

    pthread_mutex_lock(&dest->lock);
    if (dest->inbox_tail)
        dest->inbox_tail->next = msg;
    else
        dest->inbox_head = msg;
    dest->inbox_tail = msg;
    dest->inbox_len++;
    pthread_mutex_unlock(&dest->lock);
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

Message* mailbox_peek_and_match_seen(Mailbox* mbox, MessageMatchFn match, void* userdata) {
    Message* prev = NULL;
    Message* msg = mbox->seen_head;

    while (msg) {
        if (!match || match(msg, userdata)) {
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

void mailbox_clear_seen(Mailbox* mbox, heap_t *heap) {
    Message* msg = mbox->seen_head;
    while (msg) {
        Message* next = msg->next;
        if (heap) {
            if (msg->payload) heap_free(heap, msg->payload);
            heap_free(heap, msg);
        } else {
            if (msg->payload) free(msg->payload);
            free(msg);
        }
        msg = next;
    }
    mbox->seen_head = NULL;
    mbox->seen_tail = NULL;
}
