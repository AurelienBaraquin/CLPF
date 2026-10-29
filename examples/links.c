// Links propagate crashes; a process trapping exits gets them as messages
#include <api/task.h>
#include <api/message.h>
#include <api/link.h>
#include <api/monitor.h>
#include <api/io.h>

TASK0(fragile) {
    MSG_FREE(RECEIVE(MAKE_STR_TAG("crash")));
    EXIT_ERROR();
}

// Linked to a fragile child: dies with it
TASK(bystander, Pid boss) {
    Pid child = SPAWN_LINK(fragile, 0);
    SEND(boss, &child, sizeof child, MAKE_STR_TAG("child"));
    MSG_FREE(RECEIVE(MAKE_STR_TAG("never")));
}

TASK(entry, int argc, char *argv[]) {
    (void)argc;
    (void)argv;

    Pid by = SPAWN(bystander, 1, self->pid);
    MONITOR(by);
    Message *msg = RECEIVE(MAKE_STR_TAG("child"));
    Pid child = *(Pid *)msg->payload;
    MSG_FREE(msg);

    SEND(child, NULL, 0, MAKE_STR_TAG("crash"));
    Message *down = RECEIVE(MAKE_DOWN_TAG(by));
    PRINT("bystander died with its linked child (reason %u)\n", ((DownInfo *)down->payload)->reason);
    MSG_FREE(down);

    // Trapping exits turns the crash of a linked process into a message
    TRAP_EXIT(true);
    Pid other = SPAWN_LINK(fragile, 0);
    SEND(other, NULL, 0, MAKE_STR_TAG("crash"));
    Message *exit_msg = RECEIVE_EXIT();
    PRINT("trapped the exit of %ld (reason %u)\n", ((ExitInfo *)exit_msg->payload)->from,
          ((ExitInfo *)exit_msg->payload)->reason);
    MSG_FREE(exit_msg);
    EXIT_NORMAL();
}

CLPF_ENTRY(entry)
