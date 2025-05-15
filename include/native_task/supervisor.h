#include <api/base.h>
#include <utils/magic.h>

typedef enum restart_type_e {
    RES_ONE_FOR_ONE,    // If a child die restart it
    RES_ONE_FOR_ALL,    // If a child die everyone die and restart
    RES_REST_FOR_ONE,   // If a child die restart him and all the others created after him
} RestartType;

#define MAGIC_SUP_CHILD 0xC5C1D4C1

typedef struct sup_child_spec_s {
    magic_number magic;
    Funcall_t *taskcall;
    process_priority priority;
    RestartType restart;
} SupChildSpec;

TASK0(__supervisor__);
