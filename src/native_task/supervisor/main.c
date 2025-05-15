#include <api/message.h>
#include <native_task/supervisor.h>
#include <api/io.h>
#include <api/monitor.h>

void DEF(on_signal_down, Pid from, unsigned int reason) {
    write(STDOUT_FILENO, "down\n", 5);
    // PRINT("Process %ld received signal down from %ld with reason %u\n", self->pid, from, reason);
}

TASK(__supervisor__, SUP_STRAT strategy) {
    MONITOR_INIT(on_signal_down);
    Message *msg = NULL;
    bool running = true;

    __supervisor_t sv;
    sv.strategy = strategy;

    TAILQ_INIT(&sv.sup_childs);

    while (running) {
        msg = RECEIVE_RAW(NO_TAG);

        SUP_ST r = CALL(__supervisor_handler, msg, &sv);

        switch (r) {
            case SUP_NOTHING:
                break;
            case SUP_STOP:
                running = false;
                break;
            case SUP_SUCCESS:
                if (msg->payload)
                    FREE(msg->payload);
                FREE(msg);
                break;
            
            default:
                break;
        }
    }
    EXIT();
}
