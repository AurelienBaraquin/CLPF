// Request/reply between two processes, found through the registry
#include <api/task.h>
#include <api/message.h>
#include <api/registry.h>
#include <api/io.h>

#define ROUNDS 3

TASK0(counter) {
    int total = 0;
    while (1) {
        Message *msg = RECEIVE(NO_TAG);
        if (MSG_IS(msg, MAKE_STR_TAG("add")))
            total += *(int *)msg->payload;
        // A REQUEST waits for a reply carrying its own tag
        SEND(msg->sender_pid, &total, sizeof total, msg->tag);
        bool stop = MSG_IS(msg, MAKE_STR_TAG("stop"));
        MSG_FREE(msg);
        if (stop)
            EXIT_NORMAL();
    }
}

TASK(entry, int argc, char *argv[]) {
    (void)argc;
    (void)argv;

    REGISTER("counter", SPAWN0(counter));

    Pid counter_pid = WHEREIS("counter");
    for (int i = 1; i <= ROUNDS; ++i) {
        Message *reply = REQUEST(counter_pid, &i, sizeof i, MAKE_STR_TAG("add"));
        PRINT("added %d, total %d\n", i, *(int *)reply->payload);
        MSG_FREE(reply);
    }
    MSG_FREE(REQUEST(counter_pid, NULL, 0, MAKE_STR_TAG("stop")));
    EXIT_NORMAL();
}

CLPF_ENTRY(entry)
