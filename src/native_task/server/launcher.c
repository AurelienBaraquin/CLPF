#include <native_task/server/io.h>
#include <api/task.h>
#include <api/tag.h>
#include <assert.h>

TASK(__server_launcher__, Task entry, int argc, char *argv[]) {
    Pid io = SPAWN0_PRIO_NAMED(__server_io__, "server::io", PROCESS_PRIORITY_HIGH);

    Pid pid = ({
        Funcall_t funcall;
        funcall_init(&funcall, entry, 2, argc, argv);
        Pid pid = VSPAWN_NAMED(&funcall, "clpf::entry", PROCESS_PRIORITY_MEDIUM);
        pid; });

    EXIT_NORMAL();
}
