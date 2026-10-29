#pragma once

#include <api/task.h>

// Payload of an EXIT message (tag MAKE_EXIT_TAG(from)), received instead of
// dying by a process that traps exits
typedef struct {
    Pid from;
    unsigned int reason; // exit_reason
} ExitInfo;

// Links self and pid both ways: when one ends abnormally, the other ends with
// the same reason (or gets an EXIT message if it traps exits).
// Returns 0, or -1 if pid does not exist or is self.
#define LINK(pid) \
    clpf_link(self, pid)

// Returns 0, or -1 if they were not linked
#define UNLINK(pid) \
    clpf_unlink(self, pid)

// While enabled, exit signals (except KILL) arrive as EXIT messages
#define TRAP_EXIT(enabled) \
    clpf_trap_exit(self, enabled)

// Ends pid with EXIT_REASON_KILLED, even if it traps exits; pid can be self.
// Returns 0, or -1 if pid does not exist or is already ending.
#define KILL(pid) \
    clpf_kill(self, pid)

// Sends an exit signal as a link would: ignored if reason is normal, an EXIT
// message if pid traps exits, otherwise pid ends with that reason
#define SEND_EXIT(pid, reason) \
    clpf_send_exit(self, pid, reason)

#define RECEIVE_EXIT() \
    RECEIVE_IF(TM_KIND, MAKE_TAG_KIND(TAG_KIND_EXIT))

// Spawns and links in one step: the link exists before the child can run
#define SPAWN_LINK(func, argc, ...)                          \
    ({                                                       \
        Funcall_t funcall;                                   \
        funcall_init(&funcall, func, argc, ##__VA_ARGS__);   \
        clpf_spawn(self, &funcall, (SpawnOptions){ .link = true }); \
    })

int clpf_link(Process *self, Pid pid);
int clpf_unlink(Process *self, Pid pid);
void clpf_trap_exit(Process *self, bool enabled);
int clpf_kill(Process *self, Pid pid);
int clpf_send_exit(Process *self, Pid pid, unsigned int reason);
