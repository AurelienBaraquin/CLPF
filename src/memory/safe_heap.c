#include <memory/safe_heap.h>
#include <stdlib.h>
#include <sys/mman.h>

int safe_heap_create(safe_heap_t* safe_heap, size_t size) {
    SLIST_INIT(&safe_heap->pools);

    if (size < MINIMAL_HEAP_POOL_SIZE)
        size = MINIMAL_HEAP_POOL_SIZE;

    void* base = mmap(NULL, size, PROT_READ | PROT_WRITE,
                      MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if (base == MAP_FAILED)
        return -1;

    tlsf_t tlsf = tlsf_create_with_pool(base, size);
    if (!tlsf) {
        munmap(base, size);
        return -1;
    }

    safe_heap_pool_t* pool = malloc(sizeof(safe_heap_pool_t));
    if (!pool) {
        tlsf_destroy(tlsf);
        munmap(base, size);
        return -1;
    }

    pool->base = base;
    pool->size = size;
    SLIST_INSERT_HEAD(&safe_heap->pools, pool, next);

    safe_heap->tlsf = tlsf;
    spinlock_init(&safe_heap->lock);

    return 0;
}

void safe_heap_destroy(safe_heap_t* safe_heap) {
    if (!safe_heap) return;

    if (safe_heap->tlsf)
        tlsf_destroy(safe_heap->tlsf);

    safe_heap_pool_t* pool;
    while (!SLIST_EMPTY(&safe_heap->pools)) {
        pool = SLIST_FIRST(&safe_heap->pools);
        SLIST_REMOVE_HEAD(&safe_heap->pools, next);

        munmap(pool->base, pool->size);
        free(pool);
    }
}

void safe_heap_free(safe_heap_t* safe_heap, void* ptr) {
    if (!safe_heap || !ptr)
        return;

    spinlock_lock(&safe_heap->lock);
    tlsf_free(safe_heap->tlsf, ptr);
    spinlock_unlock(&safe_heap->lock);
}

int safe_heap_grow(safe_heap_t* safe_heap, size_t size) {
    if (!safe_heap)
        return -1;

    if (size <  MINIMAL_HEAP_POOL_SIZE)
        size = MINIMAL_HEAP_POOL_SIZE;

    void* base = mmap(NULL, size, PROT_READ | PROT_WRITE,
                      MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if (base == MAP_FAILED)
        return -1;
    
    spinlock_lock(&safe_heap->lock);

    void* ref = tlsf_add_pool(safe_heap->tlsf, base, size);
    if (!ref) {
        munmap(base, size);
        spinlock_unlock(&safe_heap->lock);
        return -1;
    }

    safe_heap_pool_t* pool = malloc(sizeof(safe_heap_pool_t));
    if (!pool) {
        munmap(base, size);
        spinlock_unlock(&safe_heap->lock);
        return -1;
    }

    pool->base = base;
    pool->size = size;
    SLIST_INSERT_HEAD(&safe_heap->pools, pool, next);

    spinlock_unlock(&safe_heap->lock);
    return 0;
}

void* safe_heap_alloc(safe_heap_t* safe_heap, size_t size) {
    if (!safe_heap)
        return NULL;
    if (size == 0)
        return NULL;
    
    spinlock_lock(&safe_heap->lock);
    void* ptr = tlsf_malloc(safe_heap->tlsf, size);
    spinlock_unlock(&safe_heap->lock);

    if (!ptr)
        safe_heap_grow(safe_heap, size);

    spinlock_lock(&safe_heap->lock);
    ptr = tlsf_malloc(safe_heap->tlsf, size);
    spinlock_unlock(&safe_heap->lock);

    return ptr;
}

void* safe_heap_realloc(safe_heap_t* safe_heap, void* ptr, size_t size) {
    if (!safe_heap)
        return NULL;
    if (size == 0)
        return NULL;

    spinlock_lock(&safe_heap->lock);
    void* new_ptr = tlsf_realloc(safe_heap->tlsf, ptr, size);
    spinlock_unlock(&safe_heap->lock);

    return new_ptr;
}
