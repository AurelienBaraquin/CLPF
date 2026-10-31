#include <memory/block_pool.h>

bool block_pool_put(block_pool_t *pool, void *block) {
    spinlock_lock(&pool->lock);
    bool kept = pool->count < CLPF_BLOCK_POOL_CAPACITY;
    if (kept)
        pool->blocks[pool->count++] = block;
    spinlock_unlock(&pool->lock);
    return kept;
}

void *block_pool_take(block_pool_t *pool) {
    spinlock_lock(&pool->lock);
    void *block = pool->count > 0 ? pool->blocks[--pool->count] : NULL;
    spinlock_unlock(&pool->lock);
    return block;
}

void block_pool_drain(block_pool_t *pool, void (*release)(void *block)) {
    void *block;
    while ((block = block_pool_take(pool)) != NULL)
        release(block);
}

size_t block_pool_count(block_pool_t *pool) {
    spinlock_lock(&pool->lock);
    size_t count = pool->count;
    spinlock_unlock(&pool->lock);
    return count;
}
