#include <global_supervisor.h>
#include <signal.h>
#include <clpf_internal.h>

void global_supervisor_init(GlobalSupervisor* supervisor)
{
    if (!supervisor)
        return;

    mpscq_init(&supervisor->signal_queue);
}

void global_supervisor_destroy(GlobalSupervisor* supervisor)
{
    if (!supervisor)
        return;

    mpscq_clear(&supervisor->signal_queue, free);
}

