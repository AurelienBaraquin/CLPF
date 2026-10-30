#pragma once

#include <clpf_internal.h>

// Shared binaries: large read-only data passed between processes without
// copying. A binary lives outside every process heap and counts its
// references: the allocation, and each message carrying it. It is freed with
// the last one. Write it before sharing it; after the first SEND_BIN, every
// holder may read it at once, so nobody may write it anymore.

// A new binary of size bytes, writable until shared, or NULL. The caller
// holds one reference, released with BIN_FREE or when it ends.
#define BIN_ALLOC(size) clpf_bin_alloc(self, size)
// Releases the caller's BIN_ALLOC reference: 0, or -1 if it holds none to this binary
#define BIN_FREE(data) clpf_bin_free(self, data)
// Sends the binary itself: the receiver's msg->payload points to the same
// bytes, and its MSG_FREE releases the message's reference. data is a binary
// the caller holds: its own, or the payload of a binary message it got.
// Same results as SEND, and -1 if data is not a binary.
#define SEND_BIN(pid, data, tag) clpf_send_bin(self, pid, data, tag)
// Size of a binary, and its current number of references (statistics only)
#define BIN_SIZE(data) clpf_bin_size(data)
#define BIN_REFS(data) clpf_bin_refs(data)

void *clpf_bin_alloc(Process *self, size_t size);
int clpf_bin_free(Process *self, void *data);
int clpf_send_bin(Process *self, Pid pid, void *data, Tag tag);
size_t clpf_bin_size(const void *data);
size_t clpf_bin_refs(const void *data);
