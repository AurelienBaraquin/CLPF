#include <api/message.h>
#include <native_task/supervisor.h>
#include <api/io.h>
#include <api/monitor.h>
#include <api/link.h>
#include <string.h>

#define DEFAULT_PERIOD 1000 // Default period in milliseconds
#define DEFAULT_MAX_RESTARTS 5

TASK(clpf_supervisor, SUP_STRAT strategy, uint64_t period_ms, int max_restarts) {
    supervisor_t sv;
    sv.strategy = strategy;
    sv.period_ms = period_ms > 0 ? period_ms : DEFAULT_PERIOD;
    sv.max_restarts = max_restarts > 0 ? max_restarts : DEFAULT_MAX_RESTARTS;
    sv.child_count = 0;
    sv.restart_next = 0;
    TAILQ_INIT(&sv.sup_childs);

    sv.restart_times = ALLOC((sv.max_restarts + 1) * sizeof(uint64_t));
    if (!sv.restart_times)
        EXIT_ERROR();
    memset(sv.restart_times, 0, (sv.max_restarts + 1) * sizeof(uint64_t));

    // Children are linked: trapping keeps their exits from taking the supervisor down
    TRAP_EXIT(true);

    bool running = true;
    while (running) {
        Message *msg = RECEIVE(NO_TAG);

        if (msg->tag.kind == TAG_KIND_DOWN) {
            DownInfo *down = msg->payload;
            CALL(sup_on_child_down, down->from, down->reason, &sv);
        } else if (msg->tag.kind != TAG_KIND_EXIT) { // the DOWN carries the same news
            running = CALL(supervisor_handler, msg, &sv) != SUP_STOP;
        }

        MSG_FREE(msg);
    }

    CALL(sup_stop_all, &sv);
    EXIT_NORMAL();
}
