#include <utils/run_queue.h>
#include <stdio.h>

// Initialisation
void runq_init(RunQueue *rq) {
    rq->head = NULL;
    rq->tail = NULL;
}

// Ajout en fin
void runq_push(RunQueue *rq, Process *p) {
    if (!p)
        return;

    p->next = NULL;
    if (rq->tail)
        rq->tail->next = p;
    else
        rq->head = p;
    rq->tail = p;
}

// Extraction en tête
Process *runq_pop(RunQueue *rq) {
    Process *p = rq->head;
    if (!p)
        return NULL;
    rq->head = p->next;
    if (!rq->head)
        rq->tail = NULL;
    p->next = NULL;
    return p;
}

// Extraction en tête sans suppression (Read-only)
Process *runq_peek(RunQueue *rq) {
    return rq->head;
}

// Remettre un élément en fin (ex: si on veut le relancer plus tard)
void runq_requeue(RunQueue *rq, Process *p) {
    runq_push(rq, p);
}

// Queue vide ?
int runq_empty(RunQueue *rq) {
    return rq->head == NULL;
}

// Iterateur simple pour debug
void runq_foreach(RunQueue *rq, void (*callback)(Process *)) {
    for (Process *p = rq->head; p; p = p->next)
        callback(p);
}

// Libération de la mémoire
void runq_clear(RunQueue *rq, void (*destroy)(Process *)) {
    Process *p = rq->head;
    while (p) {
        Process *next = p->next;
        if (destroy)
            destroy(p);
        free(p);
        p = next;
    }
    rq->head = NULL;
    rq->tail = NULL;
}

size_t runq_size(RunQueue *rq) {
    size_t count = 0;
    for (Process *p = rq->head; p; p = p->next)
        ++count;
    return count;
}
