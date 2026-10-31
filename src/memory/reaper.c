#include <memory/reaper.h>
#include <utils/mpscq.h>
#include <stdatomic.h>
#include <stdlib.h>

typedef struct {
    MPSCQNode node;
    void (*release)(void *block, size_t size);
    void *block;
    size_t size;
} Deferred;

static MPSCQ queue;
// Set by the first block of a batch, cleared by the reaper before it drains:
// one wakeup per batch, and none lost
static atomic_bool pending;
static void (*_Atomic wake_reaper)(void);

void reaper_set_wake(void (*wake)(void)) {
    atomic_store(&wake_reaper, wake);
}

int reaper_defer(void (*release)(void *block, size_t size), void *block, size_t size) {
    Deferred *deferred = malloc(sizeof(Deferred));
    if (!deferred)
        return -1;
    deferred->release = release;
    deferred->block = block;
    deferred->size = size;
    mpscq_push(&queue, &deferred->node);

    void (*wake)(void) = atomic_load(&wake_reaper);
    if (!atomic_exchange(&pending, true) && wake)
        wake();
    return 0;
}

void reaper_run(void) {
    atomic_store(&pending, false);
    for (MPSCQNode *node = mpscq_drain(&queue); node;) {
        Deferred *deferred = container_of(node, Deferred, node);
        node = node->next;
        deferred->release(deferred->block, deferred->size);
        free(deferred);
    }
}
