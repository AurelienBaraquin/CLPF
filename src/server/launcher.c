#include <server/io.h>
#include <api/task.h>
#include <api/tag.h>
#include <assert.h>

TASK(__server_launcher__, Task entry, int argc, char *argv[]) {
    Pid io = SPAWN0(__server_io__);
    REGISTER("server::io", io);

    Funcall_t funcall;
    funcall_init(&funcall, entry, 2, argc, argv);
    Pid pid = __clpf_task_add(&funcall, PROCESS_PRIORITY_MEDIUM, -1);
    assert(pid != -1);

    EXIT();
}
