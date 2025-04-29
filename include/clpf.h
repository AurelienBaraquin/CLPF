#pragma once

#include <process.h>

#define MAX_PROCESSES 65536 // 2^16

typedef struct {
    Process process_table[MAX_PROCESSES];

    unsigned short fix; // From index -> Emigrate to
    unsigned short tix; // Target index -> Immigrate from
} ENV;
