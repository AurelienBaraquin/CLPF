#pragma once

#include <stddef.h>
#include <pthread.h>
#include <stdbool.h>
#include <stdint.h>

typedef uintptr_t Tag;
#define NO_TAG 0

#pragma pack(push, 1)
typedef struct Message {
    Tag tag; // Always at the beginning of the message
    void *payload;
    size_t size;
    struct Message *next;
} Message;
#pragma pack(pop)

typedef bool (*MessageMatchFn)(Message *msg, void *userdata);

typedef struct Mailbox {
    // Pour lecture locale sans lock
    Message *seen_head;
    Message *seen_tail;

    // Pour réception depuis autres threads
    Message *inbox_head;
    Message *inbox_tail;
    size_t inbox_len;

    pthread_mutex_t lock;
} Mailbox;

void mailbox_init(Mailbox* mailbox);
void mailbox_destroy(Mailbox* mailbox);
void mailbox_send_message(Mailbox* dest, void* data, size_t size, Tag tag);
void mailbox_transfer_inbox_to_seen(Mailbox* mbox);
Message* mailbox_receive_seen(Mailbox* mbox);
Message* mailbox_peek_and_match_seen(Mailbox* mbox, MessageMatchFn match, void* userdata);
void mailbox_clear_seen(Mailbox* mbox);
