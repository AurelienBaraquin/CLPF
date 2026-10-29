#pragma once

#include <utils/pid.h>
#include <utils/mpscq.h>
typedef struct process_s Process;

typedef enum signal_type_e {
    SIGNAL_DOWN,
} SignalType;

typedef struct signal_s {
    SignalType type;
    Pid from;

    unsigned int reason;
} Signal;

// Queues a signal for process `to` (dropped if it no longer exists)
void signal_send(Pid to, Pid from, SignalType type, unsigned int reason);
MPSCQNode *signal_drain(Process *self);
