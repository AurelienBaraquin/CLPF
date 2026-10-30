#pragma once

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
} safe_heap_t;

int safe_heap_create(safe_heap_t *safe_heap, size_t initial_size);
int safe_heap_grow(safe_heap_t *safe_heap, size_t size);
void safe_heap_destroy(safe_heap_t *safe_heap);
void *safe_heap_alloc(safe_heap_t *safe_heap, size_t size);
void *safe_heap_realloc(safe_heap_t *safe_heap, void *ptr, size_t size);
void safe_heap_free(safe_heap_t *safe_heap, void *ptr);
// Bytes reserved by all the pools of the heap
size_t safe_heap_size(safe_heap_t *safe_heap);
