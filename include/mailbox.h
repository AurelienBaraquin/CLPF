#pragma once

#include <stddef.h>
#include <stdbool.h>
#include <stdint.h>
#include <utils/pid.h>
#include <memory/safe_heap.h>

typedef enum {
    TAG_KIND_NONE,
    TAG_KIND_INTEGER,
    TAG_KIND_STRING,
    TAG_KIND_INTERNAL,
    // Possibility to add some other types
} TagKind;

#define MAKE_INTERNAL_TAG(code)     ((Tag){ .kind = TAG_KIND_INTERNAL, .internal_code = (code) })
#define MAKE_INT_TAG(val)           ((Tag){ .kind = TAG_KIND_INTEGER, .int_value = (val) })
#define MAKE_RANGE_TAG(min, max)    MAKE_INT_TAG(((int64_t)(min) << 32) | (max))
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

typedef struct Message {
    Tag tag; // Always at the beginning of the message
    void *payload; // Allocated, like the message itself, in the receiver's heap
    size_t size;
    Pid sender_pid;
    struct Message *next;
} Message;

typedef struct Mailbox {
    // Read by the owner only, without any lock
    Message *seen_head;
    Message *seen_tail;

    // Written by senders: guarded by the owning process's slot lock
    Message *inbox_head;
    Message *inbox_tail;
} Mailbox;

void mailbox_init(Mailbox* mailbox);
// Messages live in the owner's heap and are released with it
void mailbox_destroy(Mailbox* mailbox);
// Copies data into dest_heap (the receiver's heap) and queues it, returns -1 if the allocation fails.
// The caller holds the receiver's slot lock, as for mailbox_transfer_inbox_to_seen.
int mailbox_send_message(Mailbox* dest, safe_heap_t *dest_heap, void* data, size_t size, Pid sender, Tag tag);
void mailbox_transfer_inbox_to_seen(Mailbox* mbox);
Message* mailbox_receive_seen(Mailbox* mbox);
Message* mailbox_peek_and_match_seen(Mailbox* mbox, Tag userdata);
