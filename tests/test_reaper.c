#include <memory/reaper.h>
#include <pthread.h>
#include <stdatomic.h>
#include <stdio.h>

#define THREADS    4
#define PER_THREAD 10000

static int failures = 0;

#define CHECK(cond)                                                                  \
    do {                                                                             \
        if (!(cond)) {                                                               \
            fprintf(stderr, "%s:%d: check failed: %s\n", __FILE__, __LINE__, #cond); \
            ++failures;                                                              \
        }                                                                            \
    } while (0)

static _Atomic int wakes;
static _Atomic long released_sum;
static _Atomic int released;
static long blocks[THREADS * PER_THREAD];

static void wake(void) {
    atomic_fetch_add(&wakes, 1);
}

static void release(void *block, size_t size) {
    CHECK(size == 4096);
    atomic_fetch_add(&released_sum, *(long *)block);
    atomic_fetch_add(&released, 1);
}

static void *producer(void *arg) {
    long first = *(long *)arg;
    for (long i = first; i < first + PER_THREAD; ++i) {
        blocks[i] = i;
        CHECK(reaper_defer(release, &blocks[i], 4096) == 0);
    }
    return NULL;
}

int main(void) {
    reaper_set_wake(wake);

    // Nothing to do: nothing happens
    reaper_run();
    CHECK(atomic_load(&released) == 0);

    // Blocks deferred from several threads are all released once, by reaper_run
    pthread_t threads[THREADS];
    long firsts[THREADS];
    for (int t = 0; t < THREADS; ++t) {
        firsts[t] = (long)t * PER_THREAD;
        pthread_create(&threads[t], NULL, producer, &firsts[t]);
    }
    for (int t = 0; t < THREADS; ++t)
        pthread_join(threads[t], NULL);
    CHECK(atomic_load(&released) == 0);
    reaper_run();

    long n = (long)THREADS * PER_THREAD;
    CHECK(atomic_load(&released) == n);
    CHECK(atomic_load(&released_sum) == n * (n - 1) / 2);

    // The reaper is woken once per batch, not once per block
    CHECK(atomic_load(&wakes) >= 1 && atomic_load(&wakes) < 100);
    int before = atomic_load(&wakes);
    CHECK(reaper_defer(release, &blocks[0], 4096) == 0);
    CHECK(atomic_load(&wakes) == before + 1);
    reaper_run();
    CHECK(atomic_load(&released) == n + 1);

    if (failures == 0)
        printf("reaper: all checks passed\n");
    return failures == 0 ? 0 : 1;
}
