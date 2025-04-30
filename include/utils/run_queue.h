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

// Remettre un élément en fin (ex: si on veut le relancer plus tard)
void runq_requeue(RunQueue *rq, Process *p);

// Queue vide ?
int runq_empty(RunQueue *rq);

// Iterateur simple pour debug
void runq_foreach(RunQueue *rq, void (*callback)(Process *));

// Libération de la mémoire
void runq_clear(RunQueue *rq, void (*destroy)(Process *));
