#pragma once

#include <stdatomic.h>
#include <stdbool.h>

typedef struct MPSCQ MPSCQ;
typedef struct MPSCQNode MPSCQNode;

struct MPSCQ {
    _Atomic(MPSCQNode *) head;
    MPSCQNode *tail;
};

/// Initialise la queue (heap ou stack)
void mpscq_init(MPSCQ *q);

/// Enfile un élément (thread-safe, lock-free)
void mpscq_push(MPSCQ *q, void *ptr);

/// Vide la queue en une liste FIFO (mono-thread, non thread-safe)
/// Retourne le premier élément (la liste est chainée via `next`)
void *mpscq_drain(MPSCQ *q);

/// Retourne vrai si la queue est vide
bool mpscq_empty(MPSCQ *q);

/// Libère la mémoire de la queue
void mpscq_clear(MPSCQ *q, void (*destroy)(void *));
