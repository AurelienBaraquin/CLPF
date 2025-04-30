#pragma once

#include <utils/pid.h>

typedef enum signal_type_e {
    SIGNAL_STOP,
} SignalType;

typedef struct signal_s {
    SignalType type;
    Pid from;

    struct signal_s *next;
} Signal;
