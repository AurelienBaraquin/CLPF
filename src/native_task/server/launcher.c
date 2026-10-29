#include <native_task/server/io.h>
#include <api/task.h>
#include <api/supervisor.h>
#include <api/registry.h>

TASK(clpf_server_launcher, Task entry, int argc, char *argv[]) {
    // The system supervisor outlives the user processes at shutdown, and so do its children
    Funcall_t supervisor_call;
    funcall_init(&supervisor_call, clpf_supervisor, 3, SUP_ONE_FOR_ONE, 0, 3);
    Pid sup = clpf_spawn(self, &supervisor_call, (SpawnOptions){.name = "server::supervisor", .system = true});
    atomic_store(&clpf.system_supervisor, sup);

    Pid io = SUP_ADD_CHILD_PRIO0(sup, RES_PERMANENT, clpf_server_io, "server::io", PROCESS_PRIORITY_HIGH);
    REGISTER("server::io", io);

    Funcall_t entry_call;
    funcall_init(&entry_call, entry, 2, argc, argv);
    clpf_spawn(self, &entry_call, (SpawnOptions){.name = "clpf::entry", .entry = true});

    EXIT_NORMAL();
}
