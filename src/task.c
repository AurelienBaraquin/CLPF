#include <clpf_internal.h>
#include <server/launcher.h>
#include <assert.h>

void init_task(Task entry, int argc, char *argv[])
{
    Funcall_t funcall;
    Pid pid;
    
    funcall_init(&funcall, __server_launcher__, 0);
    pid = __clpf_task_add(&funcall, PROCESS_PRIORITY_VERY_HIGH, -1);
    assert(pid != -1);

    funcall_init(&funcall, entry, 2, argc, argv);
    pid = __clpf_task_add(&funcall, PROCESS_PRIORITY_VERY_HIGH, -1);
    assert(pid != -1);
}
