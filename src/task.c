#include <clpf_internal.h>
#include <server/launcher.h>
#include <assert.h>

void init_task(Task entry, int argc, char *argv[])
{
    Funcall_t funcall;
    Pid pid;
    
    funcall_init(&funcall, __server_launcher__, 3, entry, argc, argv);
    pid = __clpf_task_add("server::launcher", &funcall, PROCESS_PRIORITY_VERY_HIGH, -1);
    assert(pid != -1);
}
