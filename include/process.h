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

typedef struct process_s {
    // Guards pid against concurrent senders while the slot is created or destroyed.
    // Initialized once with the process table, never re-initialized.
    pthread_mutex_t lock;
    Pid pid; // INVALID_PID while the slot is free
    unsigned long generation; // Incremented each time the slot is reused, kept across reuses
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

    ProcessLinks links; // children is guarded by lock
    Monitor monitor; // monitored_by is guarded by lock
    bool exit_notified; // Guarded by lock: monitors were already sent DOWN, new ones get NOPROC

    // While non-zero, monitor callbacks are not run (owner thread only)
    int callbacks_blocked;

    MPSCQ signal_queue;
    Mailbox mailbox;

    struct process_s *next; // For the scheduler's private run queues
    MPSCQNode shared_node;  // For the scheduler's shared run queues
} Process;

int process_init(Process *process, char *name, Funcall_t *taskcall, process_priority priority, Pid pid, Pid parent_pid);
void process_destroy(Process *process);

void co_entrypoint();

// Links
Pid process_get_parent_pid(Process *self);
Pid process_get_child_pid(Process *self, size_t index);
size_t process_get_child_count(Process *self);

