#include <utils/mpscq.h>
#include <stdlib.h>
#include <stdbool.h>

void mpscq_init(MPSCQ *q) {
    atomic_store(&q->head, NULL);
    q->tail = NULL;
}

void mpscq_push(MPSCQ *q, void *ptr) {
    MPSCQNode *node = malloc(sizeof(MPSCQNode));
    node->data = ptr;

    MPSCQNode *old_head;
    do {
        old_head = atomic_load(&q->head);
        node->next = old_head;
    } while (!atomic_compare_exchange_weak(&q->head, &old_head, node));
}

bool mpscq_empty(MPSCQ *q) {
    return atomic_load(&q->head) == NULL;
}

void *mpscq_drain(MPSCQ *q) {
    MPSCQNode *list = atomic_exchange(&q->head, NULL);
    q->tail = NULL;

    // Inverse la liste pour garantir FIFO
    MPSCQNode *prev = NULL, *curr = list, *next;
    while (curr) {
        next = curr->next;
        curr->next = prev;
        prev = curr;
        curr = next;
    }

    // Reconnecte la liste inversée
    MPSCQNode *fifo = prev;
    q->tail = fifo;

    // Retourne la tête de la liste
    return fifo;
}

void mpscq_clear(MPSCQ *q, void (*destroy)(void *)) {
    MPSCQNode *node = atomic_exchange(&q->head, NULL);
    q->tail = NULL;

    // Inverse la liste pour FIFO
    MPSCQNode *prev = NULL, *curr = node, *next;
    while (curr) {
        next = curr->next;
        curr->next = prev;
        prev = curr;
        curr = next;
    }

    node = prev;
    while (node) {
        MPSCQNode *next = node->next;
        if (destroy)
            destroy(node->data);
        free(node);
        node = next;
    }
}
