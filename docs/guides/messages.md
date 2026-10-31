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
  actor system, processes come and go;
- `SEND_FULL` (`-2`): the receiver was spawned with a `mailbox_limit`, and that
  many messages already wait in its mailbox (see [backpressure](#backpressure)).

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
| `MAKE_ATOM_TAG("name")` | atom | Message types, the cheapest: `"job"`, `"stats::add"` |
| `MAKE_STR_TAG("name")` | string | Names built at run time, from a temporary buffer |
| `MAKE_INT_TAG(n)` | integer | Request ids, sequence numbers |
| `MAKE_INTERNAL_TAG(code)` | code | Application-defined enums |
| `MAKE_NOTIFY_TAG("name")` | notification | `NOTIFY` / `WAIT_NOTIF` |
| `MAKE_DOWN_TAG(pid)`, `MAKE_EXIT_TAG(pid)` | runtime | [DOWN and EXIT messages](failures.md) |

An **atom** is a name interned once: every `MAKE_ATOM_TAG("job")` in the
program gives the same pointer, so atoms are compared by pointer and sent
without copying their name. `MAKE_ATOM_TAG` takes a string literal, and
caches its atom at each call site; for a name built at run time, use
`clpf_atom_tag(name)`. Atoms live until the program ends, so keep them for a
fixed set of names, not for data.

String tags are copied with the message, so they may come from a temporary
buffer, and compared with `strcmp`. Two tags are equal when they have the
same kind and the same value: `MAKE_ATOM_TAG("1")`, `MAKE_STR_TAG("1")` and
`MAKE_INT_TAG(1)` are three different tags.

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
| `TM_PREFIX` | Atom or string tags starting with the pattern's name |
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

### With a unique reference (recommended)

`REQUEST_REF(pid, data, size, timeout_ms)` tags the request with a fresh
**reference**, unique for the whole run, and waits for the answer the server
sends with `REPLY(msg, data, size)`. It returns the reply, or `NULL` if `pid`
is gone or the timeout expired (`CLPF_INFINITY` waits for ever).

- Two requests in flight can never take each other's replies.
- A reply arriving after the caller gave up is **dropped**, so it does not
  pile up in the mailbox.

```c
#include <api/task.h>
#include <api/message.h>
#include <api/io.h>

// Doubles numbers; the server does not need to know how it was called
TASK0(doubler) {
    while (1) {
        Message *msg = RECEIVE(NO_TAG);
        int result = *(int *)msg->payload * 2;
        REPLY(msg, &result, sizeof result);
        MSG_FREE(msg);
    }
}

TASK(entry, int argc, char *argv[]) {
    Pid server = SPAWN0(doubler);
    int value = 21;
    Message *reply = REQUEST_REF(server, &value, sizeof value, 1000 /* ms */);
    if (reply)
        PRINT("%d\n", *(int *)reply->payload); // 42
    else
        PRINT("no answer within a second\n");
    MSG_FREE(reply);
}

CLPF_ENTRY(entry)
```

`MAKE_REF()` gives you such a reference for your own protocols, and
`MAKE_REF_TAG(ref)` turns it into a tag.

### With a tag

`REQUEST(pid, data, size, tag)` sends a message, then waits for the reply
**carrying the same tag**, and returns it (or `NULL` if `pid` is gone).
`REQUEST_TIMEOUT(pid, data, size, tag, ms)` gives up after `ms`. The server
answers with `REPLY(msg, ...)`, or with `SEND(msg->sender_pid, ..., msg->tag)`:

```c
// client
Message *reply = REQUEST(counter, &amount, sizeof amount, MAKE_STR_TAG("add"));
int total = *(int *)reply->payload;
MSG_FREE(reply);

// server
Message *msg = RECEIVE(NO_TAG);
REPLY(msg, &total, sizeof total);
MSG_FREE(msg);
```

Two requests with the same tag in flight can take each other's replies, and
a late reply to a `REQUEST_TIMEOUT` stays in the mailbox. Prefer
`REQUEST_REF` unless the protocol needs readable tags.

!!! note "Servers that may die"
    A timeout bounds the wait, but a dead server is only noticed at the
    timeout. To react at once, monitor it and wait for the reply or its DOWN
    (see [patterns](../patterns.md#calling-a-server-that-may-die)).

## Backpressure

A process spawned with `SpawnOptions.mailbox_limit = n` accepts at most `n`
waiting messages: past that, `SEND` returns `SEND_FULL` and the message is not
queued. The sender decides what to do: drop it, retry later, slow down.
Messages from the runtime (DOWN, EXIT, timeouts, `REPLY`s, messages sent from
outside processes) always get in, so a full mailbox never hides a failure.

```c
Pid consumer = SPAWN_OPTS(slow_consumer, .mailbox_limit = 100);
if (SEND(consumer, &item, sizeof item, MAKE_STR_TAG("item")) == SEND_FULL)
    SLEEP(10); // let it catch up
```

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
  another scheduler. A process waiting in `RECEIVE` is only woken by a
  message it may take, or one from the runtime (timeouts, DOWN, exit
  signals): other messages queue up without disturbing it.
- A `RECEIVE` looks at each queued message once, however often it wakes up.
  A new `RECEIVE` starts again from the oldest message, except
  `REQUEST_REF`, which starts after the messages older than its request: a
  backlog of unrelated messages does not slow it down.
- Matchers given to `RECEIVE_IF` also run in the senders' threads, to decide
  whether to wake the receiver: they must only read the message and the
  pattern, and stay fast.
