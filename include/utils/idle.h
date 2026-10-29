#pragma once

#include <pthread.h>
#include <stdatomic.h>
#include <stdbool.h>
#include <stdint.h>

// Lets a thread sleep while it has nothing to do and be woken by others
// without polling and without lost wakeups: a waker publishes its work first,
// then calls idle_wake; the sleeper re-checks for work after announcing it sleeps.
typedef struct {
    pthread_mutex_t lock;
    pthread_cond_t cond;
    atomic_bool sleeping;
    atomic_bool signaled;
} IdleWaiter;

void idle_init(IdleWaiter *waiter);
void idle_destroy(IdleWaiter *waiter);

// Sleeps until idle_wake is called, unless has_work(arg) already returns true
void idle_wait(IdleWaiter *waiter, bool (*has_work)(void *arg), void *arg);
// Same, but also returns once the now_ms() clock reaches deadline_ms (0: no deadline)
void idle_wait_until(IdleWaiter *waiter, bool (*has_work)(void *arg), void *arg, uint64_t deadline_ms);

void idle_wake(IdleWaiter *waiter);
