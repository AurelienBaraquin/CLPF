#pragma once

#include <api/base.h>
#include <utils/magic.h>

typedef enum restart_type_e {
    RES_PERMANENT, // Always restart the child (even in normal exit)
    RES_TRANSIENT, // Restart the child only if it crash
    RES_TEMPORARY, // Never restart the child
} RestartType;

// How long a child trapping exits may take to stop before it is killed
#define SUP_SHUTDOWN_TIMEOUT_MS 5000

typedef enum sup_strat_e {
    SUP_ONE_FOR_ONE,  // Restart the child that died
    SUP_ONE_FOR_ALL,  // Stop every other child, then restart them all
    SUP_REST_FOR_ONE, // Stop the children started after it, then restart it and them
} SUP_STRAT;

#define MAGIC_SUP_CHILD 0xC5C1D4C1

typedef struct sup_child_spec_s {
    magic_number magic;
    ProcessName name;
    Funcall_t taskcall;
    process_priority priority;
    RestartType restart;
} SupChildSpec;

typedef enum supervisor_state_e {
    SUP_NOTHING,
    SUP_STOP,
    SUP_SUCCESS,
} SUP_ST;

typedef struct SupChildSlot {
    Pid pid; // INVALID_PID while stopped
    SupChildSpec spec;

    TAILQ_ENTRY(SupChildSlot) entry; // in start order
} SupChildSlot;

typedef TAILQ_HEAD(SupChildList, SupChildSlot) SupChildList;

typedef struct supervisor_s {
    SUP_STRAT strategy;
    SupChildList sup_childs;
    int child_count;

    uint64_t period_ms;
    int max_restarts;

    // Times of the last max_restarts + 1 restarts (0: unused), oldest at restart_next
    uint64_t *restart_times;
    int restart_next;
} supervisor_t;

TASK(clpf_supervisor, SUP_STRAT strategy, uint64_t period_ms, int max_restarts);

SupChildSlot *DEF(sup_child_list_insert, SupChildList *list, SupChildSpec *child);
void DEF(sup_child_list_remove, SupChildList *list, SupChildSlot *child);
SupChildSlot *sup_child_find_by_pid(supervisor_t *sv, Pid pid);

SUP_ST DEF(supervisor_handler, Message *msg, supervisor_t *sup);

// Spawns the child (monitored and linked), returns false if it could not start
bool DEF(sup_start_child, SupChildSlot *child);
// Kills every child, last started first, and waits until each is gone
void DEF(sup_stop_all, supervisor_t *sv);
void DEF(sup_on_child_down, Pid from, unsigned int reason, supervisor_t *sv);
