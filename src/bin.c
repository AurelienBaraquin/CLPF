#include <bin.h>
#include <utils/magic.h>
#include <stdint.h>
#include <stdlib.h>

#define BIN_MAGIC ((magic_number)0xB1A4B1A4)

struct Bin {
    magic_number magic; // Cleared when freed
    _Atomic size_t refs;
    size_t size;
    _Alignas(16) unsigned char data[];
};

Bin *bin_new(size_t size) {
    Bin *bin = malloc(sizeof(Bin) + size);
    if (!bin)
        return NULL;
    bin->magic = BIN_MAGIC;
    atomic_init(&bin->refs, 1);
    bin->size = size;
    return bin;
}

Bin *bin_from_data(const void *data) {
    if (!data || (uintptr_t)data % _Alignof(Bin) != 0)
        return NULL;
    Bin *bin = (Bin *)((unsigned char *)data - offsetof(Bin, data));
    return bin->magic == BIN_MAGIC ? bin : NULL;
}

void *bin_data(Bin *bin) {
    return bin->data;
}

size_t bin_size(const Bin *bin) {
    return bin->size;
}

size_t bin_refs(Bin *bin) {
    return atomic_load(&bin->refs);
}

void bin_retain(Bin *bin) {
    atomic_fetch_add_explicit(&bin->refs, 1, memory_order_relaxed);
}

void bin_release(Bin *bin) {
    // The last holder must see every write made before the other releases
    if (atomic_fetch_sub_explicit(&bin->refs, 1, memory_order_acq_rel) == 1) {
        bin->magic = 0;
        free(bin);
    }
}

void bin_hold_link(BinHold **list, BinHold *hold) {
    hold->prev = NULL;
    hold->next = *list;
    if (*list)
        (*list)->prev = hold;
    *list = hold;
}

void bin_hold_unlink(BinHold **list, BinHold *hold) {
    if (hold->prev)
        hold->prev->next = hold->next;
    else
        *list = hold->next;
    if (hold->next)
        hold->next->prev = hold->prev;
}

void bin_holds_release_all(BinHold *list) {
    for (BinHold *hold = list; hold; hold = hold->next)
        bin_release(hold->bin);
}
