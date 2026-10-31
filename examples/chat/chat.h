// A TCP chat server, and a load test that checks every delivery.
// See docs/examples/chat.md for the design.
#pragma once

#include <api/task.h>
#include <stdbool.h>

#define CHAT_LINE_MAX      512                 // Longest line, either way
#define CHAT_NICK_MAX      32                  // Longest nickname
#define CHAT_OUTPUT_MAX    ((size_t)64 * 1024) // Output a client may leave unread before it is cut off
#define CHAT_SOCKET_BUFFER 65536               // What the kernel may hold for a client, on top of that
#define CHAT_MAILBOX_LIMIT 4096                // Messages a connection may have waiting
#define CHAT_HEAP_LIMIT    ((size_t)1 << 20)   // Bytes a connection's heap may take

// Counters kept by the chat::stats process
typedef struct {
    long connections; // Accepted
    long broadcasts;  // MSG commands
    long deliveries;  // Lines handed to connections, broadcasts and private ones
    long dropped;     // Lines refused by a full mailbox
    long slow;        // Clients cut off for not reading
    long idle;        // Clients cut off for inactivity
    long crashed;     // Connections that ended with an error
} ChatStats;

// Starts the server: a supervisor keeping the acceptor and chat::stats.
// local: listen on 127.0.0.1 only. port 0 lets the system choose.
// Returns the port it listens on, or -1.
int chat_start(Process *self, int port, bool local, int idle_ms);

// Adds delta to the server's counters (any process)
void chat_count(Process *self, ChatStats delta);
// Reads the server's counters: false if the server is not running
bool chat_stats(Process *self, ChatStats *out);

// Runs the load test against a server on 127.0.0.1:port. Returns true if
// every check passed. idle_ms: the server's inactivity timeout.
bool chat_selftest(Process *self, int port, int clients, int rooms, int messages, int idle_ms);
