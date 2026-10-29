#pragma once

#include <utils/pid.h>
#include <utils/mpscq.h>
typedef struct process_s Process;

typedef enum signal_type_e {
    SIGNAL_DOWN,
} SignalType;

typedef struct signal_s {
    MPSCQNode node; // Signals are allocated in the target's heap and queued intrusively
    SignalType type;
    Pid from;

    unsigned int reason;
} Signal;

// Queues a signal for process `to` (dropped if it no longer exists)
void signal_send(Pid to, Pid from, SignalType type, unsigned int reason);
