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

void signal_send(Process *self, Pid to, SignalType type, unsigned int reason);
MPSCQNode *signal_drain(Process *self);
