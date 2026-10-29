#pragma once

#include <stddef.h>
#include "process.h"
#include "mpscq.h"

typedef struct RunQueue {
    Process *head;
    Process *tail;
    size_t size;
} RunQueue;

// Initialise an empty queue
void runq_init(RunQueue *rq);

// Append at the tail
void runq_push(RunQueue *rq, Process *p);

// Remove and return the head, or NULL
Process *runq_pop(RunQueue *rq);

// Non-zero if the queue is empty
int runq_empty(RunQueue *rq);

// Empty the queue, calling destroy (if any) on each process
void runq_clear(RunQueue *rq, void (*destroy)(Process *));

// Number of processes, O(1)
size_t runq_size(RunQueue *rq);
