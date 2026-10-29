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
    CORE_MESSAGE_TYPE_NAME_REGISTER,
    CORE_MESSAGE_TYPE_NAME_UNREGISTER,
    CORE_MESSAGE_TYPE_NAME_LOOKUP,
    CORE_MESSAGE_TYPE_PROCESS_EXIT,
} CoreMessageType;

typedef struct {
    MPSCQNode node;
    CoreMessageType op;

    Pid sender; // Sender of the message
    Pid reply_to; // Reply to this process (can be -1 if no reply is needed)

    union {
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
    Process *process_table; // MAX_PROCESSES slots, allocated by clpf_init
    int process_count;

    // Stack of free process table indices
    int *free_slots;
    int free_slot_count;

    Scheduler scheduler_table[MAX_SCHEDULERS];
    int scheduler_count;

    TrieNode *name_table;

    // Guards the free slot stack, process_count and the name table: processes
    // are created from any thread, destroyed by the main thread
    pthread_mutex_t table_lock;

    MPSCQ message_queue;
    IdleWaiter main_idle; // The main thread sleeps here while it has no core message
} CLPF;

extern CLPF clpf;

// Returns the live process with this pid with its slot locked, so it cannot be
// destroyed until process_unlock is called, or NULL if there is no such process
Process *process_lock(Pid pid);
void process_unlock(Process *process);
// Wakes the locked process if it waits, after a message or signal was queued for it
void process_notify_locked(Process *process);

void __clpf_main(Task entry_point, int argc, char *argv[]);
int __clpf_main_update();
// Takes a free process table index, or returns -1 if the table is full
int clpf_slot_alloc(void);
// Gives back the index of a destroyed process
void clpf_slot_release(int index);

// Thread-safe: queues a core message for the main thread and wakes it
void clpf_post(CoreMessage *msg);
// Wakes the main thread and every scheduler, so they notice exit_in_progress
void clpf_wake_all(void);
// Frees a core message and what it owns
void __clpf_message_destroy(CoreMessage* msg);

// Thread-safe. Creates a process on the least loaded scheduler and returns its pid, or INVALID_PID.
// monitored_by (or INVALID_PID) gets a DOWN signal when the new process exits, even if it exits right away
Pid __clpf_task_add(const char *name, Funcall_t *fun, process_priority priority, Pid parent_pid, Pid monitored_by);

// Spawns a child of self; with monitor, self is monitoring it from its very start
Pid clpf_spawn(Process *self, const char *name, Funcall_t *fun, process_priority priority, bool monitor);

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
