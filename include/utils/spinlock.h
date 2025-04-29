#pragma once

#include <stdatomic.h>
#include <stdbool.h>
#ifdef __x86_64__
    #include <immintrin.h>
#else
    #include <sched.h>
#endif

typedef struct {
    atomic_flag locked;
} spinlock_t;

__attribute__((always_inline))
static inline void spinlock_init(spinlock_t* lock) {
    atomic_flag_clear(&lock->locked);
}

__attribute__((always_inline))
static inline void spinlock_lock(spinlock_t* lock) {
    while (atomic_flag_test_and_set(&lock->locked)) {
        #ifdef __x86_64__
            _mm_pause(); // Use _mm_pause for x86
        #else
            sched_yield(); // Use sched_yield for other architectures
        #endif
    }
}

__attribute__((always_inline))
static inline void spinlock_unlock(spinlock_t* lock) {
    atomic_flag_clear(&lock->locked);
}
