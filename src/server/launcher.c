#include <server/io.h>
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
        __clpf_request_task_add(self, 1, &funcall, PROCESS_PRIORITY_MEDIUM, self->pid);
        Pid *__pid_ptr = (Pid *)receive(self, CORE_MESSAGE_TYPE_SPAWN_TAG);
        Pid __pid = *__pid_ptr;
        safe_heap_free(&self->heap, __pid_ptr);
        __pid;
    });
    REGISTER("clpf::entry", pid);

    EXIT();
}
