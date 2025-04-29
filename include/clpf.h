#pragma once

#include <scheduler.h>

#define MAX_PROCESSES 65536 // 2^16
#define MAX_SCHEDULERS 64

typedef struct {
    Process process_table[MAX_PROCESSES];
    Scheduler scheduler_table[MAX_SCHEDULERS];

    unsigned short fix; // From index -> Emigrate to
    unsigned short tix; // Target index -> Immigrate from
} ENV;

void clpf(void);
