#include <utils/mpscq.h>

void mpscq_init(MPSCQ *q) {
    atomic_store(&q->head, NULL);
}

void mpscq_push(MPSCQ *q, MPSCQNode *node) {
    MPSCQNode *old_head = atomic_load(&q->head);
    do {
        node->next = old_head;
    } while (!atomic_compare_exchange_weak(&q->head, &old_head, node));
}

bool mpscq_empty(MPSCQ *q) {
    return atomic_load(&q->head) == NULL;
}

MPSCQNode *mpscq_drain(MPSCQ *q) {
    MPSCQNode *list = atomic_exchange(&q->head, NULL);

    // Pushes stack up in LIFO order: reverse to restore FIFO
    MPSCQNode *prev = NULL;
    while (list) {
        MPSCQNode *next = list->next;
        list->next = prev;
        prev = list;
        list = next;
    }
    return prev;
}
