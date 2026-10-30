# Messages

Header: `api/message.h`

## Sending

```c
int SEND(Pid pid, void *data, size_t size, Tag tag);
```

`SEND` copies `size` bytes from `data`, together with the tag, into the
receiver's heap, and queues the copy in its mailbox. It returns at once
with:

- `0`: the message is in the mailbox;
- `-1`: the receiver does not exist (anymore), or its heap cannot hold the
  message. Sending to a dead process is not an error for the runtime: in an
  actor system, processes come and go.

`data` may be `NULL` with `size` 0: the tag alone carries the information.

!!! warning "Copy semantics"
    What travels is bytes. A pointer inside your payload still points into
    *your* memory, which the receiver must not touch. Send values: integers,
    fixed-size structs, strings (include the terminating NUL in `size`).

Messages from one sender to one receiver are received in the order they
were sent. There is no ordering between different senders.

## Receiving

| Macro | Blocks | Returns |
|---|---|---|
| `RECEIVE(tag)` | until a match | the oldest message with that tag |
| `RECEIVE_IF(matcher, pattern)` | until a match | the oldest message accepted by the matcher |
| `FETCH(tag)`, `FETCH_IF(matcher, pattern)` | no | a message, or `NULL` if none matches |
| `RECEIVE_TIMEOUT(tag, ms)`, `RECEIVE_IF_TIMEOUT(...)` | up to `ms` | a message, or `NULL` (see [timers](timers.md)) |

Blocking only parks the calling process: its scheduler thread runs other
processes in the meantime, and the process wakes up as soon as a matching
message arrives.

`NO_TAG` matches any message, so `RECEIVE(NO_TAG)` takes the oldest message
of the mailbox. That is the usual loop of a server.

### The Message

```c
typedef struct Message {
    Tag tag;
    void *payload;    // NULL when size is 0, 16-byte aligned otherwise
    size_t size;
    Pid sender_pid;   // INVALID_PID when the runtime sent it
    ...
} Message;
```

A received message **belongs to you**: release it with `MSG_FREE(msg)`
(`MSG_FREE(NULL)` does nothing). Copy what you need out of `payload` before
freeing. Messages you never free are reclaimed when your process ends, but a
long-running process that forgets them grows forever.

## Tags

| Constructor | Kind | Typical use |
|---|---|---|
| `MAKE_STR_TAG("name")` | string | Message types: `"job"`, `"stats::add"` |
| `MAKE_INT_TAG(n)` | integer | Request ids, sequence numbers |
| `MAKE_INTERNAL_TAG(code)` | code | Application-defined enums |
| `MAKE_NOTIFY_TAG("name")` | notification | `NOTIFY` / `WAIT_NOTIF` |
| `MAKE_DOWN_TAG(pid)`, `MAKE_EXIT_TAG(pid)` | runtime | [DOWN and EXIT messages](failures.md) |

String tags are copied with the message, so they may come from a temporary
buffer. Two tags are equal when they have the same kind and the same value:
`MAKE_STR_TAG("1")` and `MAKE_INT_TAG(1)` are different.

## Selective receive

`RECEIVE(tag)` skips messages that do not match: they stay in the mailbox,
in order, for a later receive. This is what makes request/reply simple. While
you wait for one reply, anything else that arrives is kept, not lost and not
processed out of turn.

```c
#include <api/task.h>
#include <api/message.h>
#include <api/io.h>

TASK0(echo) {
    for (int i = 0; i < 3; ++i) {
        Message *msg = RECEIVE(NO_TAG);
        SEND(msg->sender_pid, msg->payload, msg->size, msg->tag); // send it back
        MSG_FREE(msg);
    }
}

TASK(entry, int argc, char *argv[]) {
    Pid pid = SPAWN0(echo);
    for (int id = 1; id <= 3; ++id)
        SEND(pid, &id, sizeof id, MAKE_INT_TAG(id));

    // Take the replies in the order we want, not the order they arrive
    for (int id = 3; id >= 1; --id) {
        Message *reply = RECEIVE(MAKE_INT_TAG(id));
        PRINT("reply %d\n", *(int *)reply->payload);
        MSG_FREE(reply);
    }
}

CLPF_ENTRY(entry)
```

A receive scans the mailbox from the oldest message. A process that leaves
many unmatched messages behind makes each of its receives slower, so receive
everything your process can get.

## Matchers

`RECEIVE_IF` and `FETCH_IF` take a *matcher* and a pattern tag:

| Matcher | Accepts |
|---|---|
| `TM_SAME` | Equal tags (`NO_TAG` accepts anything). What `RECEIVE` uses |
| `TM_PREFIX` | String tags starting with the pattern string |
| `TM_GTE`, `TM_LTE` | Integer tags `>=` / `<=` the pattern |
| `TM_IN_RANGE` | Integer tags within `MAKE_RANGE_TAG(min, max)` (32-bit bounds) |
| `TM_KIND` | Any tag of the kind given by `MAKE_TAG_KIND(kind)` |

```c
#include <api/task.h>
#include <api/message.h>
#include <api/io.h>

TASK(entry, int argc, char *argv[]) {
    SEND(self->pid, NULL, 0, MAKE_STR_TAG("log::info"));
    SEND(self->pid, NULL, 0, MAKE_STR_TAG("job"));
    SEND(self->pid, NULL, 0, MAKE_STR_TAG("log::error"));
    for (int priority = 1; priority <= 9; ++priority)
        SEND(self->pid, NULL, 0, MAKE_INT_TAG(priority));

    Message *msg;
    while ((msg = FETCH_IF(TM_PREFIX, MAKE_STR_TAG("log::")))) {
        PRINT("log message: %s\n", msg->tag.str_value);
        MSG_FREE(msg);
    }

    int urgent = 0;
    while ((msg = FETCH_IF(TM_IN_RANGE, MAKE_RANGE_TAG(7, 9)))) {
        ++urgent;
        MSG_FREE(msg);
    }
    PRINT("%d urgent messages\n", urgent); // 3
}

CLPF_ENTRY(entry)
```

You can write your own matcher: any `bool fn(const Message *msg, Tag pattern)`.

```c
static bool from_pid(const Message *msg, Tag pattern) {
    return msg->sender_pid == pattern.int_value;
}
// RECEIVE_IF(from_pid, MAKE_INT_TAG(some_pid));
```

## Request and reply

`REQUEST(pid, data, size, tag)` sends a message, then waits for the reply
**carrying the same tag**, and returns it (or `NULL` if `pid` is gone). The
server side replies to `msg->sender_pid` with `msg->tag`:

```c
// client
Message *reply = REQUEST(counter, &amount, sizeof amount, MAKE_STR_TAG("add"));
int total = *(int *)reply->payload;
MSG_FREE(reply);

// server
Message *msg = RECEIVE(NO_TAG);
SEND(msg->sender_pid, &total, sizeof total, msg->tag);
MSG_FREE(msg);
```

When a client may have several requests in flight to the same server, give
each request its own tag, for instance `MAKE_INT_TAG(request_id)`, so that
replies cannot be confused.

!!! note "No timeout"
    `REQUEST` waits for ever. If the server may die, monitor it and wait with
    `RECEIVE_IF_TIMEOUT`, or for its DOWN message (see [patterns](../patterns.md#calling-a-server-that-may-die)).

## Notifications

`NOTIFY(pid, "name")` sends an empty message; `WAIT_NOTIF("name")` waits
for one and frees it. They read well for "I'm ready" style signals. The name
can be built at run time.

```c
NOTIFY(PARENT(), "ready");     // in the child
WAIT_NOTIF("ready");           // in the parent
```

## Costs

- A message is one allocation in the receiver's heap: header, payload, tag
  string. Large payloads are copied once per send.
- `SEND` never blocks and spends a few reductions; a process sending in a
  tight loop still yields regularly.
- Delivering to a waiting process wakes it in a few microseconds, even on
  another scheduler.
