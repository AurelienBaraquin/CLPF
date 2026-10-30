#pragma once

#include <clpf_internal.h>
#include <stdio.h>

// A snapshot of a process, taken under its lock (see PROCESS_INFO)
typedef struct {
    Pid pid;
    ProcessName name;            // Label given at spawn
    ProcessName registered_name; // Empty if none
    process_state state;
    process_priority priority;
    int scheduler;       // Index of the scheduler it belongs to
    size_t queued;       // Messages waiting in its mailbox
    size_t heap_bytes;   // Memory reserved by its heap
    uint64_t runs;       // Times it was resumed
    size_t links;        // Bidirectional links (see LINK)
    size_t children;     // Processes it spawned that are still alive
    size_t monitored_by; // Processes that get a DOWN when it ends
    bool trap_exit;
    bool system; // A runtime process
} ProcessInfo;

// Can be called from any thread, also outside processes
// Fills info: 0, or -1 if pid is not alive
#define PROCESS_INFO(pid, info) clpf_process_info(pid, info)
// Writes the pids of up to max live processes to buffer; returns how many are alive
#define PROCESS_LIST(buffer, max) clpf_process_list(buffer, max)

int clpf_process_info(Pid pid, ProcessInfo *info);
size_t clpf_process_list(Pid *buffer, size_t max);
// Writes a table of every live process to out. The runtime also writes it to
// stderr when the program receives SIGUSR1.
void clpf_dump(FILE *out);
const char *clpf_state_name(process_state state);
const char *clpf_priority_name(process_priority priority);
