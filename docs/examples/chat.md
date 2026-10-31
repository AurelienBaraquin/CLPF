# Example: a chat server

[`examples/chat/`](https://github.com/AurelienBaraquin/CLPF/tree/main/examples/chat)
is a complete TCP chat server, about 900 lines with its load test. This page
explains the problem, the design, and which part of CLPF solves each
difficulty.

## The problem

A chat server is the classic case of concurrency that C handles badly:

- hundreds or thousands of clients are connected at once, each mostly idle;
- a message to a room must reach every member, while members join and leave;
- some clients read slowly, or not at all: they must not slow the others
  down, nor make the server's memory grow without bound;
- some clients never say anything and hold a connection open;
- a bug in the handling of one client must not take the server down.

With threads, that is one thread per client and locks around every room, or
a single `epoll` loop where each client becomes a state machine of callbacks.
Either way, the slow client and the crashing client are hard problems.

## Running it

```bash
cmake --build build --target example_chat
./build/examples/example_chat serve 7000
```

In two other terminals:

```text
$ nc localhost 7000                 $ nc localhost 7000
NICK alice                          NICK bob
OK                                  OK
JOIN lobby                          JOIN lobby
OK                                  OK
MSG lobby hello                     FROM alice lobby hello
FROM alice lobby hello              PRIV alice hi
FROM bob - hi                       WHO lobby
                                    WHO lobby 2
```

| Command | Effect | Answer |
|---|---|---|
| `NICK name` | Takes a nickname, unique, released on disconnection | `OK`, `ERR taken` |
| `JOIN room` | Enters a room | `OK` |
| `MSG room text` | Sends to every member of the room, the sender included | `FROM nick room text` to each |
| `PRIV nick text` | Sends to one user | `FROM nick - text`, or `ERR nobody` |
| `WHO room` | Counts the members | `WHO room n` |
| `QUIT` | Leaves | `BYE` |

## The design

```text
                        chat::sup (ONE_FOR_ONE)
                         /                 \
               chat::acceptor          chat::stats
            owns the listening          counters, read
            socket and every            with REQUEST_REF
            client socket
                 |  spawns and monitors
     +-----------+-----------+-----------+
  chat::conn  chat::conn  chat::conn  ...      one process per client
     |  \         |          |
     |   group "room::lobby" (GROUP_JOIN)      rooms
     |
     registry "nick::alice" (REGISTER)         nicknames
```

### One process per connection

`connection()` in `server.c` reads like a blocking server, and blocks
nothing. Its loop waits for *anything* that concerns its client:

```c
Message *msg = RECEIVE_TIMEOUT(NO_TAG, idle_left);
if (!msg)                                   // nothing either way: idle
    ...
else if (msg->tag.kind == TAG_KIND_FD)      // the socket is ready (IO_WATCH)
    ...
else if (MSG_IS(msg, CHAT_LINE_TAG))        // a line to send to this client
    ...
```

`IO_WATCH` turns socket readiness into a message, so a single `RECEIVE`
waits for the socket, for lines from other clients and for the idle timeout
at once. The socket is non-blocking: a slow peer never blocks a scheduler.

### Nicknames: the registry

```c
if (REGISTER(name, self->pid) != 0)
    ok = reply(c, "ERR taken\n");
```

`REGISTER` is atomic: when two clients ask for the same nickname at once,
one wins. The name is released when the process ends, whatever the reason,
so a crashed connection never leaves a nickname behind. `PRIV` finds its
target with `WHEREIS`.

### Rooms: process groups

`JOIN` is `GROUP_JOIN("room::<name>")`. A process leaves all its groups when
it ends: there is no cleanup code for rooms.

### Broadcasts: shared binaries

A message to a room of 100 members is formatted once, into a binary, and the
binary itself is sent to each member:

```c
void *shared = BIN_ALLOC(length);
memcpy(shared, line, length);
for (size_t i = 0; i < count; ++i)
    SEND_BIN(members[i], shared, CHAT_LINE_TAG);
BIN_FREE(shared);
```

No member gets a copy: each connection's `msg->payload` points to the same
bytes, freed with the last reference.

### Clients that do not read

Two limits protect the server:

- each connection has a `mailbox_limit`: past it, a broadcast to it is
  dropped (`SEND_FULL`) instead of piling up;
- each connection queues at most 64 KiB of unwritten output, and the kernel
  at most 64 KiB more (`SO_SNDBUF`). A client that does not read is cut off
  when they are full. The others never wait for it: a broadcast only hands
  a reference to each connection.

### Clients that say nothing

The idle timeout is the `RECEIVE_TIMEOUT` above, measured from the last
byte read or written. An idle client gets `ERR idle` and is disconnected.

### A connection that crashes

Each connection has a heap limit (`max_heap_size`) and is monitored by the
acceptor, which **owns every socket**. When a connection ends, normally or
not, the acceptor gets its DOWN and closes its socket:

```c
} else if (msg->tag.kind == TAG_KIND_DOWN) {
    ...
    close(sockets[i].fd);
```

So a connection never has cleanup code for its socket, and a crash (the
debug command `CRASH` calls `EXIT_ERROR()`) closes that client's connection
and nothing else. The acceptor and the counters are themselves under a
supervisor.

## The load test

`example_chat selftest [users rooms messages]` starts the server on a free
port and runs simulated users against it, one process each, on real sockets:

1. every user connects, takes a nickname and joins a room, then waits;
2. on a common start signal, each sends its messages to its room;
3. each reads until it has every member's messages, and checks that each
   arrives once and in order for its sender.

At the same time it runs the hard cases:

- two users ask for the same nickname at the same moment: one must win;
- a *lurker* joins a busy room with a tiny receive window and never reads:
  it must be cut off while the room goes on;
- a silent user never sends anything: it must get `ERR idle`;
- a user makes its connection crash: its socket must close, the room goes on.

It then checks the server's counters, and exits with 0 or 1. ctest runs it
with 200 users in 10 rooms, in Debug, Release and under both sanitizers:

```text
chat selftest: 200 users in 10 rooms, 20 messages each, on port 46309
  [ ok ] every talker got every line of its room, once, in order
  [ ok ] one twin got the nickname, the other was refused
  [ ok ] the flooder got its own lines back
  [ ok ] the client that never reads was cut off
  [ ok ] the silent client was closed after the idle timeout
  [ ok ] a crashed connection was closed, the others went on
  [ ok ] the server counted every delivery
  80000 lines delivered to talkers in 0.09 s: 869565 lines/s
  server: 206 connections, 8000 broadcasts, 84417 deliveries, 0 dropped, 1 slow, 1 idle, 1 crashed
selftest passed
```

With 1000 users in 20 rooms (`selftest 1000 20 20`, Release build, 6 cores),
the server delivers a million lines in about 0.8 s, 1.3 million lines per
second, over loopback TCP. Most of the run time is the idle-timeout check,
which waits 3 seconds on purpose.

## What it took

The example also found two problems in the runtime, now fixed:

- fd watches outlived a process's DOWN message, so an owner that closed the
  socket on DOWN could see the fd number reused and refused to the next
  connection;
- ThreadSanitizer crashed when processes moved between schedulers: each
  process is now a ThreadSanitizer fiber.
