#pragma once

#include <scheduler.h>
#include <global_supervisor.h>
#include <utils/name_table.h>

#define MAX_PROCESSES 65536 // 2^16
#define MAX_SCHEDULERS 64

typedef enum core_message_type_e {
    CORE_MESSAGE_TYPE_SPAWN,
    CORE_MESSAGE_TYPE_NAME_REGISTER,
    CORE_MESSAGE_TYPE_NAME_UNREGISTER,
    CORE_MESSAGE_TYPE_NAME_LOOKUP,
    CORE_MESSAGE_TYPE_PROCESS_EXIT,
} CoreMessageType;

typedef struct {
    CoreMessageType op;

    Pid sender; // Sender of the message
    Pid reply_to; // Reply to this process (can be -1 if no reply is needed)

    union {
        struct {
            Funcall_t *taskcall;
            process_priority priority;
            Pid parent_pid;
        } spawn;

        struct {
            char name[NAME_MAX_LENGTH];
            Pid pid;
        } name_register;

        struct {
            char name[NAME_MAX_LENGTH];
        } name_unregister;

        struct {
            char name[NAME_MAX_LENGTH];
        } name_lookup;

        struct {
            Pid pid;
        } process_exit;
    } data;
} CoreMessage;

typedef struct {
    Process process_table[MAX_PROCESSES];
    int process_count;

    Scheduler scheduler_table[MAX_SCHEDULERS];
    int scheduler_count;

    TrieNode *name_table;

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
int __clpf_request_task_add(Process *self, bool need_reply, Funcall_t *fun, process_priority priority, Pid parent_pid);


// Base
void yield(Process *self);
void exiting(Process *self);


// Message

// Loop until a valid message is received (blocking)
Message* __receive_internal(Process *self, Tag userdata);
// Look for a valid message in the mailbox and return it, if not found return NULL (non-blocking)
Message* __receive_internal_fetch(Process *self, Tag userdata);
// Loop until a valid message is received (blocking)
void* receive(Process* self, Tag userdata);
// Look for a valid message in the mailbox and return it, if not found return NULL (non-blocking)
void *fetch(Process* self, Tag userdata);

// Send a message to a process
void send(Process *self, Pid pid, void* data, size_t size, Tag tag);


// Process callbacks
void handle_callbacks(Process *self);
