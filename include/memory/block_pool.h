#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <utils/spinlock.h>

// Blocks a pool keeps at most (override with -DCLPF_BLOCK_POOL_CAPACITY=...)
#ifndef CLPF_BLOCK_POOL_CAPACITY
#define CLPF_BLOCK_POOL_CAPACITY 1024
#endif

// A bounded free list of interchangeable memory blocks, shared by all threads:
// reusing a block saves mapping it, and unmapping it, which flushes the TLB of every core.
typedef struct {
    spinlock_t lock;
    size_t count;
    void *blocks[CLPF_BLOCK_POOL_CAPACITY];
} block_pool_t;

#define BLOCK_POOL_INITIALIZER     \
    {                              \
        .lock = {ATOMIC_FLAG_INIT} \
    }

// Keeps block: false if the pool is full, then the caller releases it
bool block_pool_put(block_pool_t *pool, void *block);
// The block put last, or NULL if the pool is empty
void *block_pool_take(block_pool_t *pool);
// Blocks kept now (statistics only)
size_t block_pool_count(block_pool_t *pool);
// Hands every block to release and empties the pool
void block_pool_drain(block_pool_t *pool, void (*release)(void *block));
