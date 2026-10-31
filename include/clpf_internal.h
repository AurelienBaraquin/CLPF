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

typedef struct {
    Process *process_table;        // MAX_PROCESSES slots, allocated by clpf_init
    atomic_int user_process_count; // processes that are not system ones

    // Set when the entry process ended, by the scheduler that ends it
    atomic_bool shutdown_requested;
    _Atomic unsigned int entry_exit_reason;
    _Atomic Pid system_supervisor; // supervises the system servers, set by the launcher

    // Stack of free process table indices
    int *free_slots;
    int free_slot_count;
    // Slots below it were handed out at least once: the others are still
    // untouched zeroed memory. Slot 0 is never used, so that no valid pid is 0.
    atomic_int slot_end;

    Scheduler scheduler_table[MAX_SCHEDULERS];
    int scheduler_count;

    NameTable *name_table;

    // Guards the free slot stack, the name table and every Process.registered_name:
    // processes are created and registered from any thread, and destroyed by the
    // scheduler that ran them. Schedulers keep a few free slots of their own.
    pthread_mutex_t table_lock;

    // The main thread sleeps here between timers. Schedulers wake it when the
    // entry process ends, and for every process that ends during the shutdown.
    IdleWaiter main_idle;
    atomic_uint main_events; // Counts the wakeups, so that none is lost (see clpf_wake_main)
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
// Takes a free process table index, or returns -1 if the table is full
int clpf_slot_alloc(void);
// Scans of the process table stop here (any thread)
int clpf_slot_end(void);
// Gives back the index of a destroyed process
void clpf_slot_release(int index);
// Scheduler thread: slots taken and given back from this thread go through cache
void clpf_slot_cache_attach(SlotCache *cache);

// Thread-safe: wakes the main thread after the caller changed something it watches
// (process counts, timers, the watchdog), even if it is about to fall asleep
void clpf_wake_main(void);
// Wakes the main thread and every scheduler, so they notice exit_in_progress
void clpf_wake_all(void);
// Scheduler thread, once nothing can reach an exited process: releases its
// name, its links and everything it held, destroys it and frees its slot
void clpf_process_end(Process *process);

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
// A timeout that never expires
#define CLPF_INFINITY UINT64_MAX

// Like clpf_receive, but returns NULL once timeout_ms passed without a matching message
Message *clpf_receive_timeout(Process *self, MessageMatch match, Tag pattern, uint64_t timeout_ms);
void clpf_sleep(Process *self, uint64_t ms);
// SEND result when the receiver's mailbox holds its mailbox_limit messages
#define SEND_FULL (-2)

// self may be NULL when the runtime itself sends
int clpf_send(Process *self, Pid pid, void *data, size_t size, Tag tag);
// Delivers a message on behalf of `from` without yielding (usable from any thread)
int clpf_deliver(Pid from, Pid pid, void *data, size_t size, Tag tag);
// The same with a shared binary instead of a copy (see SEND_BIN)
int clpf_deliver_bin(Pid from, Pid pid, Bin *bin, Tag tag);
Message *clpf_request(Process *self, Pid pid, void *data, size_t size, Tag tag);
Message *clpf_request_timeout(Process *self, Pid pid, void *data, size_t size, Tag tag, uint64_t timeout_ms);

// A reference unique for the whole run of the program (never 0)
// Starts the thread that writes clpf_dump to stderr on SIGUSR1 (see api/introspection.h)
void clpf_dump_signal_init(void);
// Ends that thread, before the runtime is destroyed
void clpf_dump_signal_stop(void);

// Reads CLPF_LOG_LEVEL (see api/log.h)
void clpf_log_init(void);

typedef uint64_t Ref;
Ref clpf_make_ref(void);
// Sends a request tagged with a fresh Ref and waits for the REPLY carrying it
Message *clpf_request_ref(Process *self, Pid pid, void *data, size_t size, uint64_t timeout_ms);
// Answers msg: a REPLY for a REQUEST_REF, otherwise a message with msg's own tag
int clpf_reply(Process *self, const Message *msg, void *data, size_t size);
void clpf_msg_free(Process *self, Message *msg);
