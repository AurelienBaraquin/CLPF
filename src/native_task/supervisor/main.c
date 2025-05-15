#include <api/message.h>
#include <native_task/supervisor.h>
#include <api/io.h>
#include <api/monitor.h>

void DEF(on_signal_down, Pid from, unsigned int reason, __supervisor_t *sv) {
    PRINT("Strat: %d - Process %ld received signal down from %ld with reason %u\n", sv->strategy, self->pid, from, reason);
}

TASK(__supervisor__, SUP_STRAT strategy) {
    Message *msg = NULL;
    bool running = true;
    
    __supervisor_t sv;
    sv.strategy = strategy;
    
    TAILQ_INIT(&sv.sup_childs);
    
    MONITOR_INIT(on_signal_down, &sv);

    while (running) {
        msg = RECEIVE_RAW(NO_TAG);

        SUP_ST r = CALL(__supervisor_handler, msg, &sv);

        switch (r) {
            case SUP_NOTHING:
            case SUP_SUCCESS:
                break;
            case SUP_STOP:
                running = false;
                break;
            
            default:
                break;
        }

        if (msg->payload)
            FREE(msg->payload);
        FREE(msg);
    }
    EXIT_NORMAL();
}
