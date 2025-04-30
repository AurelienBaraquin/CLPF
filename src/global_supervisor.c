#include <global_supervisor.h>
#include <signal.h>

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

    Signal *signal = NULL;
    signal = mpscq_drain(&supervisor->signal_queue);
    while (signal) {
        Signal *next_signal = signal->next;
        free(signal);
        signal = next_signal;
    }
}

