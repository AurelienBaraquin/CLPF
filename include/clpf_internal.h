#pragma once

#include <scheduler.h>
#include <utils/name_table.h>

#define PID_INDEX_BITS 16
#define MAX_PROCESSES  (1 << PID_INDEX_BITS)
#define MAX_SCHEDULERS 64

// A pid is a process table index tagged with the slot generation, so the pid
// of a dead process never designates the next process created in its slot
#define PID_MAKE(index, generation) ((Pid)(((generation) << PID_INDEX_BITS) | (index)))
#define PID_INDEX(pid)              ((pid) & (MAX_PROCESSES - 1))

typedef enum core_message_type_e {
    CORE_MESSAGE_TYPE_PROCESS_EXIT,
} CoreMessageType;

// Requests handled by the main thread
typedef struct {
    MPSCQNode node;
    CoreMessageType op;

    union {
        struct {
            Pid pid;
        } process_exit;
    } data;
} CoreMessage;

typedef struct {
    Process *process_table; // MAX_PROCESSES slots, allocated by clpf_init
    int process_count;
    int user_process_count; // processes that are not system ones

    // Set by the main thread when the entry process ended
    bool shutdown_requested;
    unsigned int entry_exit_reason;
    _Atomic Pid system_supervisor; // supervises the system servers, set by the launcher

    // Stack of free process table indices
    int *free_slots;
    int free_slot_count;

    Scheduler scheduler_table[MAX_SCHEDULERS];
    int scheduler_count;

    TrieNode *name_table;

    // Guards the free slot stack, the process counts, the name table and every
    // Process.registered_name: processes are created and registered from any
    // thread, destroyed by the main thread
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

// Runs entry_point and the runtime until the entry process ends and every
// process is gone; returns 0 if the entry process exited normally, 1 otherwise
int clpf_main(Task entry_point, int argc, char *argv[]);
int clpf_main_update();
// Takes a free process table index, or returns -1 if the table is full
int clpf_slot_alloc(void);
// Gives back the index of a destroyed process
void clpf_slot_release(int index);

// Thread-safe: queues a core message for the main thread and wakes it
void clpf_post(CoreMessage *msg);
// Wakes the main thread and every scheduler, so they notice exit_in_progress
void clpf_wake_all(void);
// Frees a core message and what it owns
void clpf_message_destroy(CoreMessage *msg);

// Thread-safe. Creates a process on the least loaded scheduler and returns its pid, or INVALID_PID.
// monitored_by (or INVALID_PID) gets a DOWN message when the new process exits, even if it exits right away
Pid clpf_task_add(Funcall_t *fun, const SpawnOptions *options, Pid parent_pid, Pid monitored_by);

// Spawns a child of self (see SpawnOptions)
Pid clpf_spawn(Process *self, Funcall_t *fun, SpawnOptions options);

typedef enum exit_reason_e exit_reason;

// Base
void yield(Process *self);
// Spends reductions; yields once the budget is used up (cooperative preemption)
void clpf_reduce(Process *self, int cost);

// Reductions charged by the runtime operations
#define REDUCTIONS_SEND  10
#define REDUCTIONS_FETCH 1
// Detaches the monitors and links of a process that is ending, sends DOWNs and
// exit signals. Called by the process itself or, for a kill, by its scheduler.
void process_terminate(Process *process, unsigned int reason);
// Sends an exit signal from the runtime (no sender process)
void clpf_signal_exit(Pid pid, unsigned int reason, bool untrappable);
// Applies an exit signal to a locked process: an EXIT message if it traps exits
// (unless untrappable), nothing for a normal reason, otherwise a pending kill
void process_exit_signal_locked(Process *target, Pid from, unsigned int reason, bool untrappable);

// Notifies the monitors and terminates the calling process: never returns.
// Not declared _Noreturn: AddressSanitizer mishandles noreturn calls made on coroutine stacks.
void exiting(Process *self, exit_reason reason);

// Message

Message *clpf_receive(Process *self, MessageMatch match, Tag pattern);
Message *clpf_fetch(Process *self, MessageMatch match, Tag pattern);
// Like clpf_receive, but returns NULL once timeout_ms passed without a matching message
Message *clpf_receive_timeout(Process *self, MessageMatch match, Tag pattern, uint64_t timeout_ms);
void clpf_sleep(Process *self, uint64_t ms);
// self may be NULL when the runtime itself sends
int clpf_send(Process *self, Pid pid, void *data, size_t size, Tag tag);
// Delivers a message on behalf of `from` without yielding (usable from any thread)
int clpf_deliver(Pid from, Pid pid, void *data, size_t size, Tag tag);
Message *clpf_request(Process *self, Pid pid, void *data, size_t size, Tag tag);
void clpf_msg_free(Process *self, Message *msg);
