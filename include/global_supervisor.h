#pragma once

#include <clpf_internal.h>

typedef struct {
    MPSCQ signal_queue;
} GlobalSupervisor;

void global_supervisor_init(GlobalSupervisor* supervisor);
void global_supervisor_destroy(GlobalSupervisor* supervisor);
