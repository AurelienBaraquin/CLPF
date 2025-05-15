#include <api/message.h>
#include <native_task/supervisor.h>
#include <api/io.h>
#include <api/monitor.h>

#define DEFAULT_PERIOD 1000 // Default period in milliseconds
#define DEFAULT_MAX_RESTARTS 5

void DEF(on_signal_down, Pid from, unsigned int reason, __supervisor_t *sv);

TASK(__supervisor__, SUP_STRAT strategy) {
    Message *msg = NULL;
    bool running = true;

    __supervisor_t sv;
    sv.strategy = strategy;
    sv.period_ms = DEFAULT_PERIOD;
    sv.max_restarts = DEFAULT_MAX_RESTARTS;

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
