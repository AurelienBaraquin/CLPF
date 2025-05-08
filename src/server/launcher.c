#include <server/io.h>
#include <api/task.h>
#include <api/tag.h>

TASK0(__server_launcher__) {
    Pid io = SPAWN0(__server_io__);
    REGISTER("server::io", io);

    EXIT();
}
