#pragma once

#define RUNNING_ON_VALGRIND

#include <memory/safe_heap.h>
#include <memory/stack.h>
#include <utils/mpscq.h>
#include <utils/pid.h>
#include <msignal.h>
#include <monitor.h>
#include <utils/funcall.h>
#include <libaco/aco.h>
#include <mailbox.h>

#define NAME_MAX_LENGTH 64

typedef void (*Task)();

typedef enum process_state_e {
    PROCESS_INIT,
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

    char __name[NAME_MAX_LENGTH]; // Name of the process used only in Core

    safe_heap_t heap;
    aco_t *acoro;
    aco_share_stack_t *share_stack;

    process_state state;
    process_priority priority;

    // == 1 for VH, H, M and == 8 for Low
    unsigned short schedule_count; // Decremented each time the process is picked from the front of the queue, and scheduled if == 0 otherwise it goes to the back of the queue

    Funcall_t taskcall;

    ProcessLinks links;
    Monitor monitor;

    MPSCQ signal_queue;
    Mailbox mailbox;

    struct process_s *next; // For Queues
} Process;

int process_init(Process *process, Funcall_t *taskcall, process_priority priority, Pid pid, Pid parent_pid);
void process_destroy(Process *process);

void co_entrypoint();

// Links
Pid process_get_parent_pid(Process *self);
Pid process_get_child_pid(Process *self, size_t index);
size_t process_get_child_count(Process *self);

// Mailbox
bool process_poll_mailbox(Process* self);
