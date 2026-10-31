#pragma once

#include <memory/safe_heap.h>
#include <memory/stack.h>
#include <utils/mpscq.h>
#include <utils/pid.h>
#include <monitor.h>
#include <utils/funcall.h>
#include <libaco/aco.h>
#include <mailbox.h>
#include <pthread.h>

#define NAME_MAX_LENGTH 64

// The pid of a free slot: no process has it, as slot 0 is never used
#define PID_FREE 0

// Work a process may do before it has to let others run (see REDUCE)
#define REDUCTION_BUDGET 2000

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

// MEDIUM comes first so that zero-initialised SpawnOptions get it
typedef enum process_priority_e {
    PROCESS_PRIORITY_MEDIUM,
    PROCESS_PRIORITY_LOW,
    PROCESS_PRIORITY_HIGH,
    PROCESS_PRIORITY_VERY_HIGH,
} process_priority;

typedef char ProcessName[NAME_MAX_LENGTH];

typedef struct process_s {
    // Guards pid against concurrent senders while the slot is created or destroyed.
    // Initialized once with the process table, never re-initialized.
    pthread_mutex_t lock;
    Pid pid;                   // PID_FREE while the slot is free
    unsigned long generation;  // Incremented each time the slot is reused, kept across reuses
    _Atomic Pid scheduler_pid; // Changes when the process migrates

    ProcessName name;            // Label given at spawn, for logs only
    ProcessName registered_name; // Name in the registry, empty if none (guarded by clpf.table_lock)

    safe_heap_t heap;
    aco_t *acoro;
    aco_share_stack_t *share_stack;
    size_t stack_size;
    size_t mailbox_limit; // Messages that may wait in the mailbox (0: no limit)
    atomic_size_t queued; // Messages waiting: incremented by deliveries under lock, decremented by the owner

    _Atomic process_state state; // Written by its scheduler, read by PROCESS_INFO
    process_priority priority;

    // Remaining skips before a low priority process is scheduled (always 0 for other priorities)
    unsigned short schedule_count;
    int reductions;        // Left before the next forced yield (owner only)
    _Atomic uint64_t runs; // Times it was resumed (relaxed: statistics only)

    Funcall_t taskcall;

    ProcessLinks links;       // children is guarded by lock
    Monitor monitor;          // monitored_by is guarded by lock
    bool exit_notified;       // Guarded by lock: monitors were already sent DOWN, new ones get NOPROC
    links_t *linked;          // Guarded by lock: bidirectional links (see LINK)
    bool trap_exit;           // Guarded by lock: exit signals arrive as EXIT messages
    int table_count;          // Tables it owns (owner only; read by the main thread once it ended)
    int group_count;          // Groups joined (owner only; read by the main thread once it ended)
    int64_t awaited_reply;    // Guarded by lock: the Ref REQUEST_REF waits for (0: none), other replies are dropped
    bool system;              // See SpawnOptions, set at creation
    bool entry;               // See SpawnOptions, set at creation
    unsigned int exit_reason; // Guarded by lock, set once exit_notified
    // Set under lock, after kill_reason: the scheduler ends the process instead of resuming it
    atomic_bool kill_pending;
    unsigned int kill_reason;

    Mailbox mailbox;
    // Guarded by lock: what a waiting RECEIVE accepts. Senders only wake the
    // process for such a message, or one from the runtime (NULL: any message).
    MessageMatch wait_match;
    Tag wait_pattern;
    BinHold *bin_holds; // Guarded by lock: shared binaries it holds (see SEND_BIN)

    struct process_s *next;   // For the scheduler's private run queues
    MPSCQNode shared_node;    // For the scheduler's shared run queues
    MPSCQNode wake_node;      // For the scheduler's wakeup queue
    atomic_bool wake_pending; // wake_node is queued: no need to queue it again
} Process;

// How to start a process (zero-initialised fields take the defaults)
typedef struct {
    const char *name;          // label for logs, "" by default
    process_priority priority; // PROCESS_PRIORITY_MEDIUM by default
    bool monitor;              // the spawner monitors the new process from its very start
    const void *arg_block;     // copied into the new process heap and passed as its argument
    size_t arg_block_size;
    size_t stack_size;    // CLPF_DEFAULT_STACK_SIZE by default
    bool link;            // links the spawner and the new process before it can run
    size_t mailbox_limit; // SEND fails with SEND_FULL once this many messages wait (0: no limit)
    int scheduler;        // 1 + index of the scheduler to start on; 0 picks the least loaded
    bool system;          // runtime-internal: survives until the user processes are gone
    bool entry;           // runtime-internal: its end shuts the runtime down
} SpawnOptions;

int process_init(Process *process, Funcall_t *taskcall, const SpawnOptions *options, Pid pid, Pid parent_pid);
void process_destroy(Process *process);
static inline bool process_slot_free(const Process *process) {
    return process->pid == PID_FREE;
}

void co_entrypoint();

// Links
Pid process_get_parent_pid(Process *self);
Pid process_get_child_pid(Process *self, size_t index);
size_t process_get_child_count(Process *self);
