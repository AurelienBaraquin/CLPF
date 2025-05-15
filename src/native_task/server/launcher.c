#include <native_task/server/io.h>
#include <api/task.h>
#include <api/tag.h>
#include <assert.h>

TASK(__server_launcher__, Task entry, int argc, char *argv[]) {
    REGISTER("server::launcher", self->pid);

    Pid io = SPAWN0_PRIO(__server_io__, PROCESS_PRIORITY_HIGH);
    REGISTER("server::io", io);

    Pid pid = ({
        Funcall_t funcall;
        funcall_init(&funcall, entry, 2, argc, argv);
        Pid pid = VSPAWN_PRIO(&funcall, PROCESS_PRIORITY_MEDIUM);
        pid; });

    REGISTER("clpf::entry", pid);

    EXIT_NORMAL();
}
