#pragma once

#include <stddef.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdatomic.h>
#include <utils/pid.h>
#include <memory/safe_heap.h>
#include <bin.h>

typedef enum {
    TAG_KIND_NONE,
    TAG_KIND_INTEGER,
    TAG_KIND_STRING,
    TAG_KIND_INTERNAL,
    TAG_KIND_NOTIFY,  // A string, distinct from TAG_KIND_STRING (see NOTIFY)
    TAG_KIND_DOWN,    // int_value is the pid of the process that went down (see MONITOR)
    TAG_KIND_TIMEOUT, // int_value is a timer id: internal to RECEIVE_TIMEOUT
    TAG_KIND_EXIT,    // int_value is the pid whose exit signal was trapped (see TRAP_EXIT)
    TAG_KIND_REF,     // int_value is a Ref: a request made with REQUEST_REF
    TAG_KIND_REPLY,   // int_value is a Ref: the answer to a REQUEST_REF (see REPLY)
    TAG_KIND_ATOM,    // str_value is an atom: an interned name, compared by pointer (see MAKE_ATOM_TAG)
    TAG_KIND_FD,      // int_value is a file descriptor that became ready (see IO_WATCH)
} TagKind;

#define MAKE_INTERNAL_TAG(code) ((Tag){.kind = TAG_KIND_INTERNAL, .internal_code = (code)})
#define MAKE_INT_TAG(val)       ((Tag){.kind = TAG_KIND_INTEGER, .int_value = (val)})
// Matched by TM_IN_RANGE: min and max are packed as two 32-bit integers
#define MAKE_RANGE_TAG(min, max) MAKE_INT_TAG((int64_t)(((uint64_t)(uint32_t)(min) << 32) | (uint32_t)(max)))
// The string is copied with the message: it may be a temporary buffer
#define MAKE_STR_TAG(str)    ((Tag){.kind = TAG_KIND_STRING, .str_value = (str)})
#define MAKE_NOTIFY_TAG(str) ((Tag){.kind = TAG_KIND_NOTIFY, .str_value = (str)})
#define MAKE_DOWN_TAG(pid)   ((Tag){.kind = TAG_KIND_DOWN, .int_value = (pid)})
#define MAKE_EXIT_TAG(pid)   ((Tag){.kind = TAG_KIND_EXIT, .int_value = (pid)})
#define MAKE_TIMEOUT_TAG(id) ((Tag){.kind = TAG_KIND_TIMEOUT, .int_value = (int64_t)(id)})
#define MAKE_REF_TAG(ref)    ((Tag){.kind = TAG_KIND_REF, .int_value = (int64_t)(ref)})
#define MAKE_REPLY_TAG(ref)  ((Tag){.kind = TAG_KIND_REPLY, .int_value = (int64_t)(ref)})
#define MAKE_FD_TAG(fd)      ((Tag){.kind = TAG_KIND_FD, .int_value = (fd)})
// An atom tag: its name is interned once, then compared by pointer and sent
// without copying, which makes it the cheapest tag to match. name must be a
// string literal: each call site keeps its atom. clpf_atom_tag takes any string.
#define MAKE_ATOM_TAG(name) ((Tag){.kind = TAG_KIND_ATOM, .str_value = CLPF_LITERAL_ATOM(name)})
#define CLPF_LITERAL_ATOM(name)                                                                     \
    ({                                                                                              \
        static const char *_Atomic clpf_atom_cache;                                                 \
        const char *clpf_atom_value = atomic_load_explicit(&clpf_atom_cache, memory_order_acquire); \
        if (!clpf_atom_value) {                                                                     \
            clpf_atom_value = clpf_atom("" name); /* "" name: only a literal compiles */            \
            atomic_store_explicit(&clpf_atom_cache, clpf_atom_value, memory_order_release);         \
        }                                                                                           \
        clpf_atom_value;                                                                            \
    })
#define MAKE_TAG_KIND(k) ((Tag){.kind = (k)})

#define NO_TAG ((Tag){.kind = TAG_KIND_NONE})

typedef struct Tag_s {
    TagKind kind;
    union {
        int64_t int_value;
        const char *str_value;
        uint64_t internal_code;
        void *custom;
    };
} Tag;

// A message is one allocation in the receiver's heap: this header, the payload,
// then the tag string if any. The receiver releases it with MSG_FREE.
typedef struct Message {
    Tag tag;
    void *payload; // NULL when size is 0
    size_t size;
    Pid sender_pid; // INVALID_PID when sent by the runtime
    Bin *bin;       // The shared binary payload points into (see SEND_BIN), or NULL
    struct Message *next;
} Message;

// Decides whether a message matches a pattern tag (see the TM_* matchers)
typedef bool (*MessageMatch)(const Message *msg, Tag pattern);

typedef struct Mailbox {
    // Read by the owner only, without any lock
    Message *seen_head;
    Message *seen_tail;

    // Written by senders: guarded by the owning process's slot lock
    Message *inbox_head;
    Message *inbox_tail;
} Mailbox;

// The atom of a name: the same pointer for every equal string, valid until the
// program ends. Thread-safe; aborts if out of memory, as a literal cannot fail.
const char *clpf_atom(const char *name);
Tag clpf_atom_tag(const char *name);

void mailbox_init(Mailbox *mailbox);
// Messages live in the owner's heap and are released with it
void mailbox_destroy(Mailbox *mailbox);
// Copies data into dest_heap (the receiver's heap) and queues it, returns -1 if the allocation fails.
// The caller holds the receiver's slot lock, as for mailbox_transfer_inbox_to_seen.
int mailbox_send_message(Mailbox *dest, safe_heap_t *dest_heap, void *data, size_t size, Pid sender, Tag tag);
// Queues a message whose payload is the binary itself, and takes a reference
// to it, listed in holds (the receiver's). Same locking as above.
int mailbox_send_bin(Mailbox *dest, safe_heap_t *dest_heap, BinHold **holds, Bin *bin, Pid sender, Tag tag);
// The reference a binary message holds
BinHold *mailbox_bin_hold(Message *msg);
void mailbox_transfer_inbox_to_seen(Mailbox *mbox);
// Unlinks and returns the oldest seen message matching pattern, or NULL (owner only)
Message *mailbox_take_matching(Mailbox *mbox, MessageMatch match, Tag pattern);
// The same, starting after *cursor, the last message a previous call looked at
// (NULL: from the start), and moving it forward. Removing the message at
// *cursor in between invalidates it.
Message *mailbox_take_matching_after(Mailbox *mbox, Message **cursor, MessageMatch match, Tag pattern);
