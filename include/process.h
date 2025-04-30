#pragma once

#include <memory/heap.h>
#include <utils/mpscq.h>
#include <utils/pid.h>
#include <signal.h>

typedef void (*Task)(void);

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

typedef struct links_s {
    Pid other_pid; // if other pid = 0, this is a link to the main core
    enum { LINK_TYPE_TO_PARENT, LINK_TYPE_TO_CHILD } type;
    struct links_s *next;
} links_t;

typedef struct process_s {
    Pid pid;
    Pid scheduler_pid;

    heap_t heap;

    process_state state;
    process_priority priority;

    // == 1 for VH, H, M and == 8 for Low
    unsigned short schedule_count; // Decremented each time the process is picked from the front of the queue, and scheduled if == 0 otherwise it goes to the back of the queue

    Task task;

    links_t *links;

    MPSCQ signal_queue;

    struct process_s *next;
} Process;

int process_init(Process *process, Task task, process_priority priority, Pid pid, Pid parent_pid);
void process_destroy(Process *process);
