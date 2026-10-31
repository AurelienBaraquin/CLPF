#include <memory/safe_heap.h>
#include <memory/block_pool.h>
#include <memory/reaper.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <sys/mman.h>
#include <errno.h>
#include <assert.h>

#define ALIGN_UP(x, align) (((x) + ((align) - 1)) & ~((align) - 1))

static void *safe_mmap(size_t size) {
    void *base = mmap(NULL, ALIGN_UP(size, 4096), PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if (base == MAP_FAILED) {
        fprintf(stderr, "safe_mmap: mmap failed (%s)\n", strerror(errno));
        return NULL;
    }
    return base;
}

static block_pool_t free_pools = BLOCK_POOL_INITIALIZER;

// Pools of the minimal size, the initial pool of every process, come from a bounded free list
static void *pool_map(size_t size) {
    void *base = size == MINIMAL_HEAP_POOL_SIZE ? block_pool_take(&free_pools) : NULL;
    return base ? base : safe_mmap(size);
}

static void unmap_minimal_pool(void *base) {
    munmap(base, MINIMAL_HEAP_POOL_SIZE);
}

static void unmap_pool(void *base, size_t size) {
    munmap(base, size);
}

static void pool_unmap(void *base, size_t size) {
    if (size == MINIMAL_HEAP_POOL_SIZE && block_pool_put(&free_pools, base))
        return;
    if (reaper_defer(unmap_pool, base, size) != 0)
        munmap(base, size);
}

size_t safe_heap_prewarm_pools(size_t count) {
    while (block_pool_count(&free_pools) < count) {
        void *base = safe_mmap(MINIMAL_HEAP_POOL_SIZE);
        if (!base)
            break;
        if (!block_pool_put(&free_pools, base)) {
            munmap(base, MINIMAL_HEAP_POOL_SIZE);
            break;
        }
    }
    return block_pool_count(&free_pools);
}

size_t safe_heap_pool_count(void) {
    return block_pool_count(&free_pools);
}

void safe_heap_drain_pools(void) {
    block_pool_drain(&free_pools, unmap_minimal_pool);
}

int safe_heap_create(safe_heap_t *safe_heap, size_t size) {
    if (!safe_heap) {
        fprintf(stderr, "safe_heap_create: null pointer\n");
        return -1;
    }

    memset(safe_heap, 0, sizeof(*safe_heap));
    SLIST_INIT(&safe_heap->pools);

    if (size < MINIMAL_HEAP_POOL_SIZE)
        size = MINIMAL_HEAP_POOL_SIZE;

    void *base = pool_map(size);
    if (!base)
        return -1;

    tlsf_t tlsf = tlsf_create_with_pool(base, size);
    if (!tlsf) {
        pool_unmap(base, size);
        fprintf(stderr, "safe_heap_create: tlsf_create_with_pool failed\n");
        return -1;
    }

    safe_heap_pool_t *pool = calloc(1, sizeof(safe_heap_pool_t));
    if (!pool) {
        tlsf_destroy(tlsf);
        pool_unmap(base, size);
        fprintf(stderr, "safe_heap_create: pool malloc failed\n");
        return -1;
    }

    pool->base = base;
    pool->size = size;
    SLIST_INSERT_HEAD(&safe_heap->pools, pool, next);

    safe_heap->tlsf = tlsf;
    spinlock_init(&safe_heap->lock);

    return 0;
}

void safe_heap_destroy(safe_heap_t *safe_heap) {
    if (!safe_heap)
        return;

    if (safe_heap->tlsf) {
        tlsf_destroy(safe_heap->tlsf);
        safe_heap->tlsf = NULL;
    }

    safe_heap_pool_t *pool;
    while (!SLIST_EMPTY(&safe_heap->pools)) {
        pool = SLIST_FIRST(&safe_heap->pools);
        SLIST_REMOVE_HEAD(&safe_heap->pools, next);

        if (pool->base && pool->size > 0)
            pool_unmap(pool->base, pool->size);
        else
            fprintf(stderr, "safe_heap_destroy: invalid pool detected\n");

        free(pool);
    }

    memset(safe_heap, 0xA5, sizeof(*safe_heap)); // poison to catch use-after-free
}

void safe_heap_free(safe_heap_t *safe_heap, void *ptr) {
    if (!safe_heap || !ptr || !safe_heap->tlsf)
        return;

    spinlock_lock(&safe_heap->lock);
    tlsf_free(safe_heap->tlsf, ptr);
    spinlock_unlock(&safe_heap->lock);
}

int safe_heap_grow(safe_heap_t *safe_heap, size_t size) {
    if (!safe_heap || !safe_heap->tlsf) {
        fprintf(stderr, "safe_heap_grow: invalid heap\n");
        return -1;
    }

    if (size < MINIMAL_HEAP_POOL_SIZE)
        size = MINIMAL_HEAP_POOL_SIZE;

    void *base = pool_map(size);
    if (!base)
        return -1;

    spinlock_lock(&safe_heap->lock);

    void *ref = tlsf_add_pool(safe_heap->tlsf, base, size);
    if (!ref) {
        pool_unmap(base, size);
        spinlock_unlock(&safe_heap->lock);
        fprintf(stderr, "safe_heap_grow: tlsf_add_pool failed\n");
        return -1;
    }

    safe_heap_pool_t *pool = calloc(1, sizeof(safe_heap_pool_t));
    if (!pool) {
        pool_unmap(base, size);
        spinlock_unlock(&safe_heap->lock);
        fprintf(stderr, "safe_heap_grow: malloc failed\n");
        return -1;
    }

    pool->base = base;
    pool->size = size;
    SLIST_INSERT_HEAD(&safe_heap->pools, pool, next);

    spinlock_unlock(&safe_heap->lock);
    return 0;
}

// A pool where an allocation of size fits: TLSF keeps its own headers in the
// pool, and looks for a free block in the size class above the request, up to
// 1/32 larger (its 32 second-level classes)
static size_t pool_size_for(size_t size) {
    return ALIGN_UP(size + size / 32 + tlsf_pool_overhead() + tlsf_alloc_overhead(), 4096);
}

void *safe_heap_alloc(safe_heap_t *safe_heap, size_t size) {
    if (!safe_heap || !safe_heap->tlsf || size == 0)
        return NULL;

    void *ptr = NULL;

    spinlock_lock(&safe_heap->lock);
    ptr = tlsf_malloc(safe_heap->tlsf, size);
    spinlock_unlock(&safe_heap->lock);

    if (!ptr) {
        if (safe_heap_grow(safe_heap, pool_size_for(size)) != 0) {
            fprintf(stderr, "safe_heap_alloc: failed to grow heap\n");
            return NULL;
        }

        spinlock_lock(&safe_heap->lock);
        ptr = tlsf_malloc(safe_heap->tlsf, size);
        spinlock_unlock(&safe_heap->lock);
    }

    if (!ptr) {
        fprintf(stderr, "safe_heap_alloc: final malloc failed\n");
    }

    return ptr;
}

void *safe_heap_realloc(safe_heap_t *safe_heap, void *ptr, size_t size) {
    if (!safe_heap || !safe_heap->tlsf || size == 0)
        return NULL;

    spinlock_lock(&safe_heap->lock);
    void *new_ptr = tlsf_realloc(safe_heap->tlsf, ptr, size);
    spinlock_unlock(&safe_heap->lock);

    if (!new_ptr) {
        fprintf(stderr, "safe_heap_realloc: realloc failed\n");
    }

    return new_ptr;
}

size_t safe_heap_size(safe_heap_t *safe_heap) {
    size_t total = 0;
    spinlock_lock(&safe_heap->lock);
    safe_heap_pool_t *pool;
    SLIST_FOREACH(pool, &safe_heap->pools, next) total += pool->size;
    spinlock_unlock(&safe_heap->lock);
    return total;
}
