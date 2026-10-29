#pragma once

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

// A low priority process is skipped this many times before being picked,
// so it runs once for every 8 passes over its run queue
#define LOW_PRIORITY_SKIPS 7

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

typedef char ProcessName[NAME_MAX_LENGTH];
static inline bool name_empty(ProcessName name) {
    return name[0] == '\0';
}

typedef struct process_s {
    Pid pid;
    Pid scheduler_pid;

    ProcessName __name;

    safe_heap_t heap;
    aco_t *acoro;
    aco_share_stack_t *share_stack;

    process_state state;
    process_priority priority;

    // Remaining skips before a low priority process is scheduled (always 0 for other priorities)
    unsigned short schedule_count;

    Funcall_t taskcall;

    ProcessLinks links;
    Monitor monitor;

    MPSCQ signal_queue;
    Mailbox mailbox;

    struct process_s *next; // For Queues
} Process;

int process_init(Process *process, char *name, Funcall_t *taskcall, process_priority priority, Pid pid, Pid parent_pid);
void process_destroy(Process *process);

void co_entrypoint();

// Links
Pid process_get_parent_pid(Process *self);
Pid process_get_child_pid(Process *self, size_t index);
size_t process_get_child_count(Process *self);

