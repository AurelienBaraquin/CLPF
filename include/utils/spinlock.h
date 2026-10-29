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

// Tells the CPU we are busy-waiting
__attribute__((always_inline))
static inline void cpu_relax(void) {
    #ifdef __x86_64__
        _mm_pause();
    #else
        sched_yield();
    #endif
}

__attribute__((always_inline))
static inline void spinlock_lock(spinlock_t* lock) {
    while (atomic_flag_test_and_set(&lock->locked))
        cpu_relax();
}

__attribute__((always_inline))
static inline void spinlock_unlock(spinlock_t* lock) {
    atomic_flag_clear(&lock->locked);
}
