#pragma once

#include <scheduler.h>
#include <global_supervisor.h>

#define MAX_PROCESSES 65536 // 2^16
#define MAX_SCHEDULERS 64

typedef enum core_message_type_e {
    CORE_MESSAGE_TYPE_SPAWN,
} CoreMessageType;

typedef struct {
    CoreMessageType op;

    union {
        struct {
            Funcall_t *taskcall;
            process_priority priority;
            Pid parent_pid;
        } spawn;
    } data;
} CoreMessage;

typedef struct {
    Process process_table[MAX_PROCESSES];
    int process_count;

    Scheduler scheduler_table[MAX_SCHEDULERS];
    int scheduler_count;

    unsigned short fix; // From index -> Emigrate to
    unsigned short tix; // Target index -> Immigrate from

    GlobalSupervisor global_supervisor;

    MPSCQ message_queue;
} CLPF;

extern CLPF clpf;

void __clpf_main(Task entry_point, int argc, char *argv[]);
int __clpf_main_update();

// This function is not thread-safe and should be used with caution only in the main thread (use __clpf_request_task_add for thread-safe task creation)
Pid __clpf_task_add(Funcall_t *fun, process_priority priority, Pid parent_pid);

// Thread-safe task creation
Pid __clpf_request_task_add(Funcall_t *fun, process_priority priority, Pid parent_pid);


// Yield
void yield(Process *self);
