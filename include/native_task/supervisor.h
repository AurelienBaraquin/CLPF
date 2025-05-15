#include <api/base.h>
#include <utils/magic.h>

typedef enum restart_type_e {
    RES_PERMANENT, // Always restart the child (even in normal exit)
    RES_TRANSIENT, // Restart the child only if it crash
    RES_TEMPORARY, // Never restart the child
} RestartType;

typedef enum sup_strat_e {
    SUP_ONE_FOR_ONE,    // If a child die restart it
    SUP_ONE_FOR_ALL,    // If a child die everyone die and restart
    SUP_REST_FOR_ONE,   // If a child die restart him and all the others created after him
} SUP_STRAT;

#define MAGIC_SUP_CHILD 0xC5C1D4C1

typedef struct sup_child_spec_s {
    magic_number magic;
    Funcall_t taskcall;
    process_priority priority;
    RestartType restart;
} SupChildSpec;

typedef enum __supervisor_state__e {
    SUP_NOTHING,
    SUP_STOP,
    SUP_SUCCESS,
} SUP_ST;

typedef struct SupChildSlot {
    Pid pid;
    SupChildSpec spec;
    int restart_count;

    uint64_t last_restart_times[10];
    int restart_index;

    TAILQ_ENTRY(SupChildSlot) entry;
} SupChildSlot;

typedef TAILQ_HEAD(SupChildList, SupChildSlot) SupChildList;

typedef struct __supervisor_s {
    SUP_STRAT strategy;
    SupChildList sup_childs;

    uint64_t period_ms;
    int max_restarts;
} __supervisor_t;

TASK(__supervisor__, SUP_STRAT strategy);

SupChildSlot* DEF(__sup_child_list_insert, SupChildList *list, SupChildSpec *child);
void DEF(__sup_child_list_remove, SupChildList *list, Pid pid);
SupChildSlot *DEF(__sup_child_find_by_pid, __supervisor_t *sv, Pid pid);

SUP_ST DEF(__supervisor_handler, Message *msg, __supervisor_t *sup);
