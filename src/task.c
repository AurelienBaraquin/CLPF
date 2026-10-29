#include <clpf_internal.h>
#include <native_task/server/launcher.h>
#include <stdio.h>

int init_task(Task entry, int argc, char *argv[]) {
    Funcall_t funcall;

    funcall_init(&funcall, clpf_server_launcher, 3, entry, argc, argv);
    SpawnOptions options = {.name = "server::launcher", .priority = PROCESS_PRIORITY_VERY_HIGH, .system = true};
    if (clpf_task_add(&funcall, &options, INVALID_PID, INVALID_PID) == INVALID_PID) {
        fprintf(stderr, "Failed to start the launcher process\n");
        return -1;
    }
    return 0;
}
