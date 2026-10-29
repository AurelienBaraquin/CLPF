#include <clpf_internal.h>
#include <native_task/server/launcher.h>
#include <assert.h>

void init_task(Task entry, int argc, char *argv[]) {
    Funcall_t funcall;
    Pid pid;

    funcall_init(&funcall, clpf_server_launcher, 3, entry, argc, argv);
    SpawnOptions options = {.name = "server::launcher", .priority = PROCESS_PRIORITY_VERY_HIGH, .system = true};
    pid = clpf_task_add(&funcall, &options, INVALID_PID, INVALID_PID);
    assert(pid != -1);
}
