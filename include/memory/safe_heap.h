#pragma once

#include <stddef.h>
#include <sys/queue.h>
#include <tlsf/tlsf.h>
#include <utils/spinlock.h>

#define MINIMAL_HEAP_POOL_SIZE (16 * 1024)  // ~16  Ko

typedef struct heap_pool_s {
    void* base;
    size_t size;
    SLIST_ENTRY(heap_pool_s) next;
} heap_pool_t;

typedef struct {
    tlsf_t tlsf;
    SLIST_HEAD(pool_list_head, heap_pool_s) pools;
    spinlock_t lock; // Spinlock for thread-safe access
} heap_t;

int heap_create(heap_t* heap, size_t initial_size);
int heap_grow(heap_t* heap, size_t size);
void heap_destroy(heap_t* heap);
void* heap_alloc(heap_t* heap, size_t size);
void* heap_realloc(heap_t* heap, void* ptr, size_t size);
void heap_free(heap_t* heap, void* ptr);
