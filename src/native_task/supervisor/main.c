#include <api/message.h>
#include <native_task/supervisor.h>
#include <api/io.h>
#include <api/monitor.h>

#define DEFAULT_PERIOD 1000 // Default period in milliseconds
#define DEFAULT_MAX_RESTARTS 5

void DEF(on_signal_down, Pid from, unsigned int reason, __supervisor_t *sv);

TASK(__supervisor__, SUP_STRAT strategy, uint64_t period_ms, int max_restarts) {
    Message *msg = NULL;
    bool running = true;

    if (strategy != SUP_ONE_FOR_ONE) {
        fprintf(stderr, "[SUPERVISOR] Only SUP_ONE_FOR_ONE is implemented\n");
        EXIT_ERROR();
    }
    if (max_restarts >= SUP_RESTART_HISTORY) {
        fprintf(stderr, "[SUPERVISOR] max_restarts must be below %d\n", SUP_RESTART_HISTORY);
        EXIT_ERROR();
    }

    __supervisor_t sv;
    sv.strategy = strategy;
    sv.period_ms = period_ms > 0 ? period_ms : DEFAULT_PERIOD;
    sv.max_restarts = max_restarts > 0 ? max_restarts : DEFAULT_MAX_RESTARTS;
    sv.child_count = 0;
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
