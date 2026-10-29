#include <utils/run_queue.h>
#include <stdio.h>

void runq_init(RunQueue *rq) {
    rq->head = NULL;
    rq->tail = NULL;
    rq->size = 0;
}

void runq_push(RunQueue *rq, Process *p) {
    if (!p)
        return;

    p->next = NULL;
    if (rq->tail)
        rq->tail->next = p;
    else
        rq->head = p;
    rq->tail = p;
    rq->size++;
}

Process *runq_pop(RunQueue *rq) {
    Process *p = rq->head;
    if (!p)
        return NULL;
    rq->head = p->next;
    if (!rq->head)
        rq->tail = NULL;
    p->next = NULL;
    rq->size--;
    return p;
}

int runq_empty(RunQueue *rq) {
    return rq->head == NULL;
}

void runq_clear(RunQueue *rq, void (*destroy)(Process *)) {
    Process *p = rq->head;
    while (p) {
        Process *next = p->next;
        if (destroy)
            destroy(p);
        p = next;
    }
    rq->head = NULL;
    rq->tail = NULL;
    rq->size = 0;
}

size_t runq_size(RunQueue *rq) {
    return rq->size;
}
