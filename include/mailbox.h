#pragma once

#include <stddef.h>
#include <pthread.h>
#include <stdbool.h>
#include <stdint.h>

typedef struct heap_s heap_t;

typedef enum {
    TAG_KIND_NONE,
    TAG_KIND_INTEGER,
    TAG_KIND_STRING,
    TAG_KIND_INTERNAL,
    // Possibility to add some other types
} TagKind;

#define MAKE_INTERNAL_TAG(code)     ((Tag){ .kind = TAG_KIND_INTERNAL, .internal_code = (code) })
#define MAKE_INT_TAG(val)           ((Tag){ .kind = TAG_KIND_INTEGER, .int_value = (val) })
#define MAKE_RANGE_TAG(min, max)    MAKE_INT_TAG(((int64_t)min << 32) | max);
#define MAKE_STR_TAG(str)           ((Tag){ .kind = TAG_KIND_STRING,  .str_value = (str) })
#define MAKE_TAG_KIND(k)            ((Tag){ .kind = (k) })

#define NO_TAG                      ((Tag){ .kind = TAG_KIND_NONE })

typedef struct Tag_s {
    TagKind kind;
    union {
        int64_t int_value;
        const char* str_value;
        uint64_t internal_code;
        void* custom;
    };
} Tag;

#pragma pack(push, 1)
typedef struct Message {
    Tag tag; // Always at the beginning of the message
    heap_t *heap; // Heap used to allocate the message
    void *payload;
    size_t size;
    struct Message *next;
} Message;
#pragma pack(pop)

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
void mailbox_destroy(Mailbox* mailbox, heap_t* heap);
void mailbox_send_message(Mailbox* dest, heap_t *heap, void* data, size_t size, Tag tag);
void mailbox_transfer_inbox_to_seen(Mailbox* mbox);
Message* mailbox_receive_seen(Mailbox* mbox);
Message* mailbox_peek_and_match_seen(Mailbox* mbox, Tag userdata);
void mailbox_clear_seen(Mailbox* mbox, heap_t *heap);
