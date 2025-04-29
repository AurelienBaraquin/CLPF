#include <memory/heap.h>
#include <stdlib.h>
#include <sys/mman.h>

int heap_create(heap_t* heap, size_t size) {
    SLIST_INIT(&heap->pools);

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

    heap_pool_t* pool = malloc(sizeof(heap_pool_t));
    if (!pool) {
        tlsf_destroy(tlsf);
        munmap(base, size);
        return -1;
    }

    pool->base = base;
    pool->size = size;
    SLIST_INSERT_HEAD(&heap->pools, pool, next);

    heap->tlsf = tlsf;

    return 0;
}

void heap_destroy(heap_t* heap) {
    if (!heap) return;

    if (heap->tlsf)
        tlsf_destroy(heap->tlsf);

    heap_pool_t* pool;
    while (!SLIST_EMPTY(&heap->pools)) {
        pool = SLIST_FIRST(&heap->pools);
        SLIST_REMOVE_HEAD(&heap->pools, next);

        munmap(pool->base, pool->size);
        free(pool);
    }
}

void heap_free(heap_t* heap, void* ptr) {
    if (!heap || !ptr)
        return;

    tlsf_free(heap->tlsf, ptr);
}

int heap_grow(heap_t* heap, size_t size) {
    if (!heap)
        return -1;

    if (size <  MINIMAL_HEAP_POOL_SIZE)
        size = MINIMAL_HEAP_POOL_SIZE;

    void* base = mmap(NULL, size, PROT_READ | PROT_WRITE,
                      MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if (base == MAP_FAILED)
        return -1;
    

    void* ref = tlsf_add_pool(heap->tlsf, base, size);
    if (!ref) {
        munmap(base, size);

        return -1;
    }

    heap_pool_t* pool = malloc(sizeof(heap_pool_t));
    if (!pool) {
        munmap(base, size);

        return -1;
    }

    pool->base = base;
    pool->size = size;
    SLIST_INSERT_HEAD(&heap->pools, pool, next);

    return 0;
}

void* heap_alloc(heap_t* heap, size_t size) {
    if (!heap)
        return NULL;
    if (size == 0)
        return NULL;
    
    void* ptr = tlsf_malloc(heap->tlsf, size);

    if (!ptr)
        heap_grow(heap, size);

    ptr = tlsf_malloc(heap->tlsf, size);

    return ptr;
}

void* heap_realloc(heap_t* heap, void* ptr, size_t size) {
    if (!heap)
        return NULL;
    if (size == 0)
        return NULL;

    void* new_ptr = tlsf_realloc(heap->tlsf, ptr, size);

    return new_ptr;
}
