#pragma once

#include <scheduler.h>

#define MAX_PROCESSES 65536 // 2^16
#define MAX_SCHEDULERS 64

typedef struct {
    Process process_table[MAX_PROCESSES];
    int process_count;

    Scheduler scheduler_table[MAX_SCHEDULERS];
    int scheduler_count;

    unsigned short fix; // From index -> Emigrate to
    unsigned short tix; // Target index -> Immigrate from
} CLPF;

void clpf(Task entry_point);
