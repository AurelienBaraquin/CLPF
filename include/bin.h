#pragma once

#include <stdatomic.h>
#include <stdbool.h>
#include <stddef.h>

// A shared binary: one block outside every process heap, read by all the
// processes that hold a reference to it (see api/bin.h)
typedef struct Bin Bin;

// One reference held by a process: its own allocation, or a message it got.
// Each process lists them, under its lock, to release them when it ends.
typedef struct BinHold {
    Bin *bin;
    bool owned; // From BIN_ALLOC, released with BIN_FREE; otherwise by its message
    struct BinHold *prev;
    struct BinHold *next;
} BinHold;

// A new binary with one reference, or NULL
Bin *bin_new(size_t size);
// The binary whose data this is, or NULL if data does not come from bin_new
Bin *bin_from_data(const void *data);
void *bin_data(Bin *bin);
size_t bin_size(const Bin *bin);
size_t bin_refs(Bin *bin);
void bin_retain(Bin *bin);
// Frees the binary with its last reference
void bin_release(Bin *bin);

void bin_hold_link(BinHold **list, BinHold *hold);
void bin_hold_unlink(BinHold **list, BinHold *hold);
// Releases every reference of a list; the nodes themselves live in the dying process's heap
void bin_holds_release_all(BinHold *list);
