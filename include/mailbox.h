#pragma once

#include <stddef.h>
#include <pthread.h>
#include <stdbool.h>

typedef struct Message {
    struct Message* next;       // pour la queue
    size_t size;                // taille utile
    void* payload;              // pointeur vers les données (à toi de définir)
} Message;

typedef struct Mailbox {
    Message* head;
    Message* tail;
    size_t   len;

    pthread_mutex_t lock;
} Mailbox;

typedef bool (*MessageMatchFn)(Message* msg, void* userdata);

void mailbox_init(Mailbox* mailbox);
void mailbox_destroy(Mailbox* mailbox);

void mailbox_send_message(Mailbox* dest, void* data, size_t size);
Message* mailbox_receive_message(Mailbox* self);

Message* mailbox_peek_and_match(Mailbox* self, MessageMatchFn match, void* userdata);
