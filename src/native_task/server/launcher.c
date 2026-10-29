#include <native_task/server/io.h>
#include <api/task.h>
#include <api/supervisor.h>
#include <api/registry.h>
#include <assert.h>

TASK(clpf_server_launcher, Task entry, int argc, char *argv[]) {
    Pid sup = SPAWN_SUPERVISOR("server::supervisor", SUP_ONE_FOR_ONE, 0, 3);
    Pid io = SUP_ADD_CHILD_PRIO0(sup, RES_TRANSIENT, clpf_server_io, "server::io", PROCESS_PRIORITY_HIGH);
    REGISTER("server::io", io);

    Funcall_t funcall;
    funcall_init(&funcall, entry, 2, argc, argv);
    VSPAWN_NAMED(&funcall, "clpf::entry", PROCESS_PRIORITY_MEDIUM);

    EXIT_NORMAL();
}
