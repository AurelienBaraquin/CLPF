#include <memory/safe_heap.h>
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

int safe_heap_create(safe_heap_t *safe_heap, size_t size) {
    if (!safe_heap) {
        fprintf(stderr, "safe_heap_create: null pointer\n");
        return -1;
    }

    memset(safe_heap, 0, sizeof(*safe_heap));
    SLIST_INIT(&safe_heap->pools);

    if (size < MINIMAL_HEAP_POOL_SIZE)
        size = MINIMAL_HEAP_POOL_SIZE;

    void *base = safe_mmap(size);
    if (!base)
        return -1;

    tlsf_t tlsf = tlsf_create_with_pool(base, size);
    if (!tlsf) {
        munmap(base, size);
        fprintf(stderr, "safe_heap_create: tlsf_create_with_pool failed\n");
        return -1;
    }

    safe_heap_pool_t *pool = calloc(1, sizeof(safe_heap_pool_t));
    if (!pool) {
        tlsf_destroy(tlsf);
        munmap(base, size);
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
            munmap(pool->base, pool->size);
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

    void *base = safe_mmap(size);
    if (!base)
        return -1;

    spinlock_lock(&safe_heap->lock);

    void *ref = tlsf_add_pool(safe_heap->tlsf, base, size);
    if (!ref) {
        munmap(base, size);
        spinlock_unlock(&safe_heap->lock);
        fprintf(stderr, "safe_heap_grow: tlsf_add_pool failed\n");
        return -1;
    }

    safe_heap_pool_t *pool = calloc(1, sizeof(safe_heap_pool_t));
    if (!pool) {
        munmap(base, size);
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

void *safe_heap_alloc(safe_heap_t *safe_heap, size_t size) {
    if (!safe_heap || !safe_heap->tlsf || size == 0)
        return NULL;

    void *ptr = NULL;

    spinlock_lock(&safe_heap->lock);
    ptr = tlsf_malloc(safe_heap->tlsf, size);
    spinlock_unlock(&safe_heap->lock);

    if (!ptr) {
        if (safe_heap_grow(safe_heap, size) != 0) {
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
