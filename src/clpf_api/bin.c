#include <api/bin.h>
#include <bin.h>

void *clpf_bin_alloc(Process *self, size_t size) {
    BinHold *hold = safe_heap_alloc(&self->heap, sizeof(BinHold));
    Bin *bin = hold ? bin_new(size) : NULL;
    if (!bin) {
        safe_heap_free(&self->heap, hold);
        return NULL;
    }

    hold->bin = bin;
    hold->owned = true;
    pthread_mutex_lock(&self->lock);
    bin_hold_link(&self->bin_holds, hold);
    pthread_mutex_unlock(&self->lock);
    return bin_data(bin);
}

int clpf_bin_free(Process *self, void *data) {
    Bin *bin = bin_from_data(data);
    if (!bin)
        return -1;

    pthread_mutex_lock(&self->lock);
    BinHold *hold = self->bin_holds;
    while (hold && !(hold->owned && hold->bin == bin))
        hold = hold->next;
    if (hold)
        bin_hold_unlink(&self->bin_holds, hold);
    pthread_mutex_unlock(&self->lock);
    if (!hold)
        return -1;

    safe_heap_free(&self->heap, hold);
    bin_release(bin);
    return 0;
}

int clpf_send_bin(Process *self, Pid pid, void *data, Tag tag) {
    Bin *bin = bin_from_data(data);
    if (!bin)
        return -1;
    int delivered = clpf_deliver_bin(self->pid, pid, bin, tag);
    clpf_reduce(self, REDUCTIONS_SEND);
    return delivered;
}

size_t clpf_bin_size(const void *data) {
    Bin *bin = bin_from_data(data);
    return bin ? bin_size(bin) : 0;
}

size_t clpf_bin_refs(const void *data) {
    Bin *bin = bin_from_data(data);
    return bin ? bin_refs(bin) : 0;
}
