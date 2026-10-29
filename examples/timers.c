// Delayed messages, timeouts and sleeping without blocking a scheduler
#include <api/task.h>
#include <api/message.h>
#include <api/timer.h>
#include <api/io.h>

TASK(entry, int argc, char *argv[]) {
    (void)argc;
    (void)argv;

    SEND_AFTER(self->pid, 30, NULL, 0, MAKE_STR_TAG("tick"));
    TimerId never = SEND_AFTER(self->pid, 10, NULL, 0, MAKE_STR_TAG("cancelled"));
    CANCEL_TIMER(never);

    Message *early = RECEIVE_TIMEOUT(MAKE_STR_TAG("tick"), 10);
    PRINT("after 10 ms: %s\n", early ? "tick" : "nothing yet");
    MSG_FREE(early);

    Message *tick = RECEIVE_TIMEOUT(MAKE_STR_TAG("tick"), 1000);
    PRINT("then: %s\n", tick ? "tick" : "timeout");
    MSG_FREE(tick);

    SLEEP(20);
    Message *cancelled = FETCH(MAKE_STR_TAG("cancelled"));
    PRINT("cancelled timer fired: %s\n", cancelled ? "yes" : "no");
    MSG_FREE(cancelled);
    EXIT_NORMAL();
}

CLPF_ENTRY(entry)
