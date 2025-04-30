#pragma once

#include <scheduler.h>
#include <global_supervisor.h>

#define MAX_PROCESSES 65536 // 2^16
#define MAX_SCHEDULERS 64

typedef struct {
    Process process_table[MAX_PROCESSES];
    int process_count;

    Scheduler scheduler_table[MAX_SCHEDULERS];
    int scheduler_count;

    unsigned short fix; // From index -> Emigrate to
    unsigned short tix; // Target index -> Immigrate from

    GlobalSupervisor global_supervisor;
} CLPF;

extern CLPF clpf;

void clpf_main(Task entry_point);

void clpf_task_add(Task task, process_priority priority, Pid parent_pid);
