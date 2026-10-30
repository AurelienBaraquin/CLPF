#include <memory/block_pool.h>
#include <memory/safe_heap.h>
#include <memory/stack.h>
#include <pthread.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define THREADS           4
#define BLOCKS_PER_THREAD 64
#define ROUNDS            2000
#define LARGE_STACK_SIZE  ((size_t)64 * 1024)

static int failures = 0;

#define CHECK(cond)                                                                  \
    do {                                                                             \
        if (!(cond)) {                                                               \
            fprintf(stderr, "%s:%d: check failed: %s\n", __FILE__, __LINE__, #cond); \
            ++failures;                                                              \
        }                                                                            \
    } while (0)

static size_t released;

static void count_release(void *block) {
    (void)block;
    ++released;
}

static void test_block_pool(void) {
    static block_pool_t pool = BLOCK_POOL_INITIALIZER;
    static char blocks[CLPF_BLOCK_POOL_CAPACITY + 1];

    CHECK(block_pool_take(&pool) == NULL);

    // Last in, first out: the block most likely still in the caches comes back first
    CHECK(block_pool_put(&pool, &blocks[0]));
    CHECK(block_pool_put(&pool, &blocks[1]));
    CHECK(block_pool_take(&pool) == &blocks[1]);
    CHECK(block_pool_take(&pool) == &blocks[0]);
    CHECK(block_pool_take(&pool) == NULL);

    // Bounded: the caller releases what does not fit
    for (size_t i = 0; i < CLPF_BLOCK_POOL_CAPACITY; ++i)
        CHECK(block_pool_put(&pool, &blocks[i]));
    CHECK(!block_pool_put(&pool, &blocks[CLPF_BLOCK_POOL_CAPACITY]));

    released = 0;
    block_pool_drain(&pool, count_release);
    CHECK(released == CLPF_BLOCK_POOL_CAPACITY);
    CHECK(block_pool_take(&pool) == NULL);
}

static block_pool_t shared_pool = BLOCK_POOL_INITIALIZER;
static int shared_blocks[(size_t)THREADS * BLOCKS_PER_THREAD];

// Each thread hands its blocks over, then takes blocks put by any thread and puts them back
static void *exchange_blocks(void *arg) {
    int *own = arg;
    for (int i = 0; i < BLOCKS_PER_THREAD; ++i) {
        if (!block_pool_put(&shared_pool, &own[i]))
            return &own[i];
    }
    for (int round = 0; round < ROUNDS; ++round) {
        int *held[BLOCKS_PER_THREAD];
        int count = 0;
        while (count < BLOCKS_PER_THREAD && (held[count] = block_pool_take(&shared_pool)) != NULL)
            ++*held[count++];
        while (count > 0) {
            if (!block_pool_put(&shared_pool, held[--count]))
                return held[count];
        }
    }
    return NULL;
}

static void test_block_pool_threads(void) {
    pthread_t threads[THREADS];
    for (int i = 0; i < THREADS; ++i)
        pthread_create(&threads[i], NULL, exchange_blocks, &shared_blocks[(size_t)i * BLOCKS_PER_THREAD]);
    for (int i = 0; i < THREADS; ++i) {
        void *lost = NULL;
        pthread_join(threads[i], &lost);
        CHECK(lost == NULL);
    }

    // No block lost or duplicated: each one is in the pool exactly once
    static bool seen[sizeof shared_blocks / sizeof shared_blocks[0]];
    size_t count = 0;
    for (int *block = block_pool_take(&shared_pool); block; block = block_pool_take(&shared_pool)) {
        ptrdiff_t index = block - shared_blocks;
        CHECK(index >= 0 && (size_t)index < sizeof shared_blocks / sizeof shared_blocks[0]);
        CHECK(!seen[index]);
        seen[index] = true;
        ++count;
    }
    CHECK(count == sizeof shared_blocks / sizeof shared_blocks[0]);
}

// Permissions of the mapping holding address, from /proc/self/maps ("rw-p", "r--p"...)
static bool mapping_permissions(const void *address, char permissions[5]) {
    FILE *maps = fopen("/proc/self/maps", "r");
    if (!maps)
        return false;
    char line[512];
    bool found = false;
    while (!found && fgets(line, sizeof line, maps)) {
        char *cursor = NULL;
        uintptr_t start = strtoul(line, &cursor, 16);
        if (*cursor != '-')
            continue;
        uintptr_t end = strtoul(cursor + 1, &cursor, 16);
        found = (uintptr_t)address >= start && (uintptr_t)address < end && strlen(cursor) > 4;
        if (found)
            memcpy(permissions, cursor + 1, 4);
    }
    fclose(maps);
    return found;
}

static void test_stack_pool(void) {
    aco_share_stack_t *first = stack_acquire(CLPF_DEFAULT_STACK_SIZE);
    CHECK(first != NULL);
    if (!first)
        return;
    void *real_ptr = first->real_ptr;
    memset(first->ptr, 0x5A, first->sz); // as a process would, up to its return address slot
    stack_release(first, CLPF_DEFAULT_STACK_SIZE);

    // The released default stack comes back, ready for a new coroutine
    aco_share_stack_t *again = stack_acquire(CLPF_DEFAULT_STACK_SIZE);
    CHECK(again == first);
    CHECK(again->real_ptr == real_ptr);
    CHECK(again->sz >= CLPF_DEFAULT_STACK_SIZE);
    CHECK(again->owner == NULL);
    CHECK(again->align_validsz == 0);
    CHECK(*(void **)again->align_retptr == (void *)aco_funcp_protector_asm);

    // Its guard page is still read-only, so an overflow still faults on it
    char permissions[5] = {0};
    CHECK(mapping_permissions(again->real_ptr, permissions));
    CHECK(strcmp(permissions, "r--p") == 0);
    CHECK(mapping_permissions(again->ptr, permissions));
    CHECK(strcmp(permissions, "rw-p") == 0);

    // Stacks of other sizes bypass the pool both ways
    stack_release(again, CLPF_DEFAULT_STACK_SIZE);
    aco_share_stack_t *large = stack_acquire(LARGE_STACK_SIZE);
    CHECK(large != NULL && large != again);
    if (!large)
        return;
    CHECK(large->sz >= LARGE_STACK_SIZE);
    stack_release(large, LARGE_STACK_SIZE);
    aco_share_stack_t *large_again = stack_acquire(LARGE_STACK_SIZE);
    CHECK(large_again != again);
    CHECK(large_again->sz >= LARGE_STACK_SIZE);
    stack_release(large_again, LARGE_STACK_SIZE);

    // The pooled stack is unmapped, not leaked
    stack_pool_drain();
}

int main(void) {
    test_block_pool();
    test_block_pool_threads();
    test_stack_pool();

    if (failures)
        fprintf(stderr, "%d check(s) failed\n", failures);
    return failures ? 1 : 0;
}
