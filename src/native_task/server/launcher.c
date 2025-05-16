#include <native_task/server/io.h>
#include <api/task.h>
#include <api/supervisor.h>
#include <api/tag.h>
#include <assert.h>

TASK(__server_launcher__, Task entry, int argc, char *argv[]) {
    Pid sup = SPAWN_SUPERVISOR("server::supervisor", SUP_ONE_FOR_ONE, 0, 3);
    Pid io = SUP_ADD_CHILD_PRIO0(sup, RES_TRANSIENT, __server_io__, "server::io", PROCESS_PRIORITY_HIGH);

    Pid pid = ({
        Funcall_t funcall;
        funcall_init(&funcall, entry, 2, argc, argv);
        Pid pid = VSPAWN_NAMED(&funcall, "clpf::entry", PROCESS_PRIORITY_MEDIUM);
        pid; });

    EXIT_NORMAL();
}
