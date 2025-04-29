#pragma once

#include <memory/heap.h>

typedef enum process_state_e {
    PROCESS_RUNNING,
    PROCESS_READY,
    PROCESS_WAITING,
    PROCESS_EXITING,
} process_state;

typedef enum process_priority_e {
    PROCESS_PRIORITY_LOW,
    PROCESS_PRIORITY_MEDIUM,
    PROCESS_PRIORITY_HIGH,
    PROCESS_PRIORITY_VERY_HIGH,
} process_priority;

typedef struct process_s {
    int pid;
    
    heap_t heap;

    process_state state;
    process_priority priority;

    TAILQ_ENTRY(process_s) next;
} Process;
