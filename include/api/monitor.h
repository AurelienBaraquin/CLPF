#pragma once

#include <api/message.h>

// Payload of a DOWN message, tagged MAKE_DOWN_TAG(from)
typedef struct {
    Pid from;
    unsigned int reason; // exit_reason
} DownInfo;

// From then on, a DOWN message is sent to self when target exits; right away,
// with EXIT_REASON_NOPROC, if target does not exist (anymore)
#define MONITOR(target) \
    process_monitor(self, target)

// Stops monitoring target; a DOWN already received stays in the mailbox
#define DEMONITOR(target) \
    process_demonitor(self, target)

// Waits for the DOWN of any monitored process (RECEIVE(MAKE_DOWN_TAG(pid)) for a given one)
#define RECEIVE_DOWN() \
    RECEIVE_IF(TM_KIND, MAKE_TAG_KIND(TAG_KIND_DOWN))

void process_monitor(Process *self, Pid target);
void process_demonitor(Process *self, Pid target);
