#pragma once

#include <scheduler.h>
#include <utils/name_table.h>

#define PID_INDEX_BITS 16
#define MAX_PROCESSES (1 << PID_INDEX_BITS)
#define MAX_SCHEDULERS 64

// A pid is a process table index tagged with the slot generation, so the pid
// of a dead process never designates the next process created in its slot
#define PID_MAKE(index, generation) ((Pid)(((generation) << PID_INDEX_BITS) | (index)))
#define PID_INDEX(pid) ((pid) & (MAX_PROCESSES - 1))

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
            char name[NAME_MAX_LENGTH];
            Funcall_t *taskcall;
            process_priority priority;
            Pid parent_pid;
            bool monitor; // The sender monitors the new process from its very start
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

    MPSCQ message_queue;
} CLPF;

extern CLPF clpf;

// Returns the live process with this pid, or NULL. The result is only stable
// on the main thread, which is the only one creating and destroying processes.
Process *process_get(Pid pid);
// Same as process_get, but keeps the slot locked so the process cannot be
// destroyed until process_unlock is called
Process *process_lock(Pid pid);
void process_unlock(Process *process);

void __clpf_main(Task entry_point, int argc, char *argv[]);
int __clpf_main_update();

// This function is not thread-safe and should be used with caution only in the main thread (use __clpf_request_task_add for thread-safe task creation)
// monitored_by (or INVALID_PID) gets a DOWN signal when the new process exits, even if it exits right away
Pid __clpf_task_add(char *name, Funcall_t *fun, process_priority priority, Pid parent_pid, Pid monitored_by);

// Thread-safe task creation
int __clpf_request_task_add(Process *self, char *name, bool need_reply, bool monitor, Funcall_t *fun, process_priority priority, Pid parent_pid);

typedef enum exit_reason_e exit_reason;

// Base
void yield(Process *self);
// Notifies the monitors and terminates the calling process: never returns.
// Not declared _Noreturn: AddressSanitizer mishandles noreturn calls made on coroutine stacks.
void exiting(Process *self, exit_reason reason);


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
int send(Process *self, Pid pid, void* data, size_t size, Tag tag);

// Request (send + receive with the same tag)
void* request(Process *self, Pid pid, void* data, size_t size, Tag tag);



// Runs the monitor callbacks of pending signals, unless callbacks_blocked is set
void handle_callbacks(Process *self);
