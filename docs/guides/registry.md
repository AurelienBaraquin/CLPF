# Registry

Header: `api/registry.h`

A pid changes every time a process is restarted, so long-lived services are
found by **name**:

| Macro | Returns |
|---|---|
| `REGISTER(name, pid)` | `0`, or `-1` if the name is invalid or taken, `pid` does not exist, or it already has a name |
| `UNREGISTER(name)` | `0`, or `-1` if the name was not registered |
| `WHEREIS(name)` | The registered pid, or `INVALID_PID` |

Rules:

- Names use `A-Z`, `a-z`, `0-9`, `_` and `:`, and are at most 63
  characters. `::` is a handy separator: `"db::pool"`.
- A process has at most one registered name.
- The name is released when the process ends. A restarted process can take
  it again right away, even before the old process is fully cleaned up.
- The name given at spawn (`SPAWN_NAMED`) is only a **label** for logs. It is
  not registered, and several processes may share a label.

All three calls are synchronous and cheap (a lookup under a lock), so you can
call `WHEREIS` before every use instead of caching a pid that may have died.

```c
#include <api/task.h>
#include <api/message.h>
#include <api/registry.h>
#include <api/io.h>

TASK0(config_server) {
    REGISTER("config", self->pid); // register yourself on start, also after a restart
    while (1) {
        Message *msg = RECEIVE(NO_TAG);
        int timeout_ms = 250;
        SEND(msg->sender_pid, &timeout_ms, sizeof timeout_ms, msg->tag);
        MSG_FREE(msg);
    }
}

TASK(entry, int argc, char *argv[]) {
    Pid server = SPAWN0(config_server);
    while (WHEREIS("config") == INVALID_PID) // until it has registered
        YIELD();

    Message *reply = REQUEST(WHEREIS("config"), NULL, 0, MAKE_STR_TAG("timeout"));
    PRINT("timeout is %d ms\n", *(int *)reply->payload);
    MSG_FREE(reply);
    (void)server;
}

CLPF_ENTRY(entry)
```

To avoid the wait loop, the spawner can register the child itself right after
the spawn: `REGISTER("config", SPAWN0(config_server))`. The pid exists as soon
as `SPAWN` returns.

The runtime registers its I/O server as `server::io`.
