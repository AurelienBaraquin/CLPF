# Groups and shared data

Messages are the default way to share data: each process gets its own copy,
so nobody can corrupt anyone else's state. Three tools cover the cases where
copies are not the right fit:

| Tool | Header | For |
|---|---|---|
| [Groups](#groups) | `api/group.h` | Reaching a changing set of processes with one send |
| [Shared binaries](#shared-binaries) | `api/bin.h` | Large read-only data, sent without a copy |
| [Shared tables](#shared-tables) | `api/table.h` | Data many processes read often: configuration, caches, counters |

## Groups

| Macro | Meaning |
|---|---|
| `GROUP_JOIN(name)` | `0`, or `-1` if the name is invalid or out of memory. Joining twice is joining once |
| `GROUP_LEAVE(name)` | `0`, or `-1` if the caller is not a member |
| `GROUP_MEMBERS(name, buffer, max)` | Writes up to `max` member pids to `buffer`, in join order; returns how many there are |
| `GROUP_SEND(name, data, size, tag)` | Sends a copy to every member, the caller too if it is one; returns how many got it |

A group exists while it has members; names are any string of at most 63
characters. A process may belong to several groups, and leaves them all
when it ends, so a group never lists a dead process for long.

Groups suit publish/subscribe: subscribers join a topic, publishers send to
it without knowing who listens.

```c
#include <api/task.h>
#include <api/message.h>
#include <api/group.h>
#include <api/io.h>

TASK(subscriber, int id, Pid boss) {
    GROUP_JOIN("prices");
    SEND(boss, NULL, 0, MAKE_STR_TAG("ready"));
    Message *price = RECEIVE(MAKE_STR_TAG("price"));
    PRINT("subscriber %d got %d\n", id, *(int *)price->payload);
    MSG_FREE(price);
    SEND(boss, NULL, 0, MAKE_STR_TAG("done"));
}

TASK(entry, int argc, char *argv[]) {
    for (int i = 0; i < 3; ++i) {
        SPAWN(subscriber, 2, i, self->pid);
        MSG_FREE(RECEIVE(MAKE_STR_TAG("ready")));
    }

    int price = 42;
    size_t reached = GROUP_SEND("prices", &price, sizeof price, MAKE_STR_TAG("price"));
    PRINT("published to %zu subscribers\n", reached);
    for (size_t i = 0; i < reached; ++i)
        MSG_FREE(RECEIVE(MAKE_STR_TAG("done")));
}

CLPF_ENTRY(entry)
```

`GROUP_SEND` reads the members, then sends to each one: a process that
joins during the send may or may not get the message.

## Shared binaries

| Macro | Meaning |
|---|---|
| `BIN_ALLOC(size)` | A new binary of `size` bytes, or `NULL`. The caller holds one reference |
| `SEND_BIN(pid, data, tag)` | Sends the binary itself, not a copy. Same results as `SEND`, and `-1` if `data` is not a binary |
| `BIN_FREE(data)` | Releases the caller's `BIN_ALLOC` reference: `0`, or `-1` if it holds none |
| `BIN_SIZE(data)`, `BIN_REFS(data)` | Its size, and its current number of references |

`SEND` copies its data into the receiver's heap: fine for small messages,
costly for a megabyte read by ten processes. A binary is a block outside
every process heap that counts its references: the allocation, and each
message that carries it. The receiver's `msg->payload` points to the very
same bytes, and `MSG_FREE` releases the message's reference. The block is
freed with the last reference.

A binary is **read-only once shared**: write it completely, then send it.
After the first `SEND_BIN`, every holder may read it at any moment, and
nothing would protect a writer from them.

```c
#include <api/task.h>
#include <api/message.h>
#include <api/bin.h>
#include <api/io.h>
#include <string.h>

#define IMAGE_BYTES (4 * 1024 * 1024)

TASK(analyser, Pid boss) {
    Message *image = RECEIVE(MAKE_STR_TAG("image"));
    const unsigned char *pixels = image->payload; // no copy: the shared bytes
    size_t bright = 0;
    for (size_t i = 0; i < image->size; ++i) {
        bright += pixels[i] > 128;
        if (i % 4096 == 0)
            REDUCE(10);
    }
    SEND(boss, &bright, sizeof bright, MAKE_STR_TAG("result"));
    MSG_FREE(image); // releases this process's reference
}

TASK(entry, int argc, char *argv[]) {
    unsigned char *image = BIN_ALLOC(IMAGE_BYTES);
    if (!image)
        EXIT_ERROR();
    memset(image, 200, IMAGE_BYTES); // written before it is shared

    for (int i = 0; i < 4; ++i)
        SEND_BIN(SPAWN(analyser, 1, self->pid), image, MAKE_STR_TAG("image"));
    BIN_FREE(image); // the messages keep it alive

    for (int i = 0; i < 4; ++i) {
        Message *result = RECEIVE(MAKE_STR_TAG("result"));
        PRINT("%zu bright pixels\n", *(size_t *)result->payload);
        MSG_FREE(result);
    }
}

CLPF_ENTRY(entry)
```

Rules:

- `data` given to `SEND_BIN` and `BIN_FREE` must be the start of a binary:
  a `BIN_ALLOC` result, or the `payload` of a message received from
  `SEND_BIN`. Forwarding a received binary with `SEND_BIN(other, msg->payload, tag)`
  is how a binary moves along a pipeline.
- A process that ends releases everything it holds: its allocations, the
  binary messages it received and never freed, and those still queued.
- Binaries do not count in the process heap, so `ALLOC` and `mailbox_limit`
  do not bound them.

## Shared tables

| Macro | Meaning |
|---|---|
| `TABLE_NEW(name)` | A new table, owned by the caller; its `TableId`, or `0` if the name is invalid or taken |
| `TABLE_FIND(name)` | The `TableId` with that name, or `0` |
| `TABLE_PUT(id, key, data, size)` | Stores a copy under `key` (a string), replacing any value: `0` or `-1` |
| `TABLE_GET(id, key, &size)` | A copy of the value in the caller's heap (`FREE` it), its size in `size` (may be `NULL`), or `NULL` |
| `TABLE_DELETE(id, key)` | `0`, or `-1` if there is no such key |
| `TABLE_COUNT(id)` | The number of keys, or `-1` if the table does not exist |
| `TABLE_DROP(id)` | Deletes the table: `0`, or `-1` if the caller does not own it |

A table is a named key-value store, like Erlang's ETS. Any process may read
and write it without a message round trip: reads run in parallel, a write
has the table to itself for the time of a copy. Values are copied in and
out, so a process never keeps a pointer into the table.

A table belongs to the process that created it and is deleted when that
process ends. Operations on a deleted table fail, and its `TableId` never
designates a later table, even one with the same name. Put a table's owner
under a supervisor, and have it fill the table again when it restarts.

```c
#include <api/task.h>
#include <api/message.h>
#include <api/memory.h>
#include <api/table.h>
#include <api/io.h>

TASK(worker, int id, Pid boss) {
    TableId config = TABLE_FIND("config");
    int *limit = TABLE_GET(config, "rate_limit", NULL);
    if (limit) {
        PRINT("worker %d: limit %d\n", id, *limit);
        FREE(limit);
    }
    SEND(boss, NULL, 0, MAKE_STR_TAG("done"));
}

TASK(entry, int argc, char *argv[]) {
    TableId config = TABLE_NEW("config");
    int limit = 100;
    TABLE_PUT(config, "rate_limit", &limit, sizeof limit);

    for (int i = 0; i < 3; ++i)
        SPAWN(worker, 2, i, self->pid);
    for (int i = 0; i < 3; ++i)
        MSG_FREE(RECEIVE(MAKE_STR_TAG("done")));
    PRINT("%ld key(s)\n", TABLE_COUNT(config));
}

CLPF_ENTRY(entry)
```

When to use a table instead of a server process: many readers, small
values, and no need for several keys to change together. A server process
stays the right tool when an update must check and change several values at
once, since a table only makes each operation atomic on its own.
