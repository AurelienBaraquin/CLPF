#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <sys/queue.h>
#include <tlsf/tlsf.h>
#include <utils/spinlock.h>

#define MINIMAL_HEAP_POOL_SIZE ((size_t)16 * 1024)

typedef struct safe_heap_pool_s {
    void *base;
    size_t size;
    SLIST_ENTRY(safe_heap_pool_s) next;
} safe_heap_pool_t;

typedef struct safe_heap_s {
    tlsf_t tlsf;
    SLIST_HEAD(pool_list_head, safe_heap_pool_s) pools;
    spinlock_t lock; // Spinlock for thread-safe access
    size_t max_size; // Bytes all pools may take together, 0: no limit
    bool over_limit; // Guarded by lock: a growth was refused because of max_size
} safe_heap_t;

int safe_heap_create(safe_heap_t *safe_heap, size_t initial_size);
int safe_heap_grow(safe_heap_t *safe_heap, size_t size);
void safe_heap_destroy(safe_heap_t *safe_heap);
void *safe_heap_alloc(safe_heap_t *safe_heap, size_t size);
void *safe_heap_realloc(safe_heap_t *safe_heap, void *ptr, size_t size);
void safe_heap_free(safe_heap_t *safe_heap, void *ptr);
// Whether an allocation failed because of max_size
bool safe_heap_over_limit(safe_heap_t *safe_heap);
// Bytes reserved by all the pools of the heap
size_t safe_heap_size(safe_heap_t *safe_heap);
// Unmaps the pools that destroyed heaps left for the next ones
void safe_heap_drain_pools(void);
// Maps count initial pools ahead of time, as far as the pool has room; returns how many it holds then
size_t safe_heap_prewarm_pools(size_t count);
size_t safe_heap_pool_count(void);
