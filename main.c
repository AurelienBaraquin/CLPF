#include <api/task.h>
#include <api/run.h>
#include <api/message.h>
#include <api/base.h>
#include <api/tag.h>
#include <stdio.h>
#include <string.h>

TASK0(named) {
    REGISTER("named_proc", self->pid);
    printf("Registered under 'named_proc'\n");

    NOTIFY(PARENT(), "ready");

    int *msg = RECEIVE(NULL, NULL);
    printf("Named received: %d\n", *msg);
    safe_heap_free(&self->heap, msg);

    UNREGISTER("named_proc");
    printf("Unregistered 'named_proc'\n");
    EXIT();
}

TASK(task, int argc, char *argv[]) {
    SPAWN0(named);

    WAIT_NOTIF("ready");

    Pid target = WHEREIS("named_proc");
    printf("Found 'named_proc' with pid: %ld\n", target);
    int x = 42;
    SEND(target, &x, sizeof(int));

    while (WHEREIS("named_proc") != -1) {
        yield(self);  // ou sleep court
    }

    Pid res = WHEREIS("named_proc");
    printf("Should not find anymore: %ld\n", res);

    EXIT();
}

CLPF_ENTRY(task)
