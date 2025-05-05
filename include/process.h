#pragma once

#define RUNNING_ON_VALGRIND

#include <memory/heap.h>
#include <memory/stack.h>
#include <utils/mpscq.h>
#include <utils/pid.h>
#include <signal.h>
#include <links.h>
#include <ucontext.h>
#include <utils/funcall.h>
#include <libco/libco.h>

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

typedef struct process_s {
    Pid pid;
    Pid scheduler_pid;

    heap_t heap;
    cothread_t coro;

    process_state state;
    process_priority priority;

    // == 1 for VH, H, M and == 8 for Low
    unsigned short schedule_count; // Decremented each time the process is picked from the front of the queue, and scheduled if == 0 otherwise it goes to the back of the queue

    Funcall_t taskcall;

    links_t *links;

    MPSCQ signal_queue;

    struct process_s *next; // For Queues
} Process;

int process_init(Process *process, Funcall_t *taskcall, process_priority priority, Pid pid, Pid parent_pid);
void process_destroy(Process *process);

void co_entrypoint();
