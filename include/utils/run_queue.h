#pragma once

#include <stddef.h>
#include "process.h"
#include "mpscq.h"

typedef struct RunQueue {
    Process *head;
    Process *tail;
} RunQueue;

// Initialisation
void runq_init(RunQueue *rq);

// Ajout en fin
void runq_push(RunQueue *rq, Process *p);

// Extraction en tête
Process *runq_pop(RunQueue *rq);

// Queue vide ?
int runq_empty(RunQueue *rq);

// Libération de la mémoire
void runq_clear(RunQueue *rq, void (*destroy)(Process *));

// Taille de la queue
size_t runq_size(RunQueue *rq);
