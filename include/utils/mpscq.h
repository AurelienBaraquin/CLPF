#pragma once

#include <stdatomic.h>
#include <stdbool.h>
#include <stddef.h>

// Intrusive multi-producer single-consumer queue: the node lives inside the
// queued element, so pushing never allocates. Producers push lock-free, the
// single consumer takes everything at once with mpscq_drain.

typedef struct MPSCQNode {
    struct MPSCQNode *next;
} MPSCQNode;

typedef struct MPSCQ {
    _Atomic(MPSCQNode *) head;
} MPSCQ;

#define container_of(ptr, type, member) ((type *)((char *)(ptr)-offsetof(type, member)))

void mpscq_init(MPSCQ *q);

// Thread-safe, lock-free
void mpscq_push(MPSCQ *q, MPSCQNode *node);

// Consumer only: detaches every queued node and returns them in FIFO order, linked through next
MPSCQNode *mpscq_drain(MPSCQ *q);

bool mpscq_empty(MPSCQ *q);
