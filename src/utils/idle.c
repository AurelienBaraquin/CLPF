#include <utils/idle.h>
#include <time.h>

void idle_init(IdleWaiter *waiter) {
    pthread_mutex_init(&waiter->lock, NULL);

    // Deadlines use the monotonic clock, like now_ms()
    pthread_condattr_t attributes;
    pthread_condattr_init(&attributes);
    pthread_condattr_setclock(&attributes, CLOCK_MONOTONIC);
    pthread_cond_init(&waiter->cond, &attributes);
    pthread_condattr_destroy(&attributes);
    atomic_init(&waiter->sleeping, false);
    atomic_init(&waiter->signaled, false);
}

void idle_destroy(IdleWaiter *waiter) {
    pthread_mutex_destroy(&waiter->lock);
    pthread_cond_destroy(&waiter->cond);
}

void idle_wait(IdleWaiter *waiter, bool (*has_work)(void *arg), void *arg) {
    idle_wait_until(waiter, has_work, arg, 0);
}

void idle_wait_until(IdleWaiter *waiter, bool (*has_work)(void *arg), void *arg, uint64_t deadline_ms) {
    atomic_store(&waiter->sleeping, true);
    atomic_store(&waiter->signaled, false);

    // Work published before this point is seen here; work published later
    // comes with an idle_wake that finds sleeping set
    if (!has_work(arg)) {
        struct timespec deadline = {(time_t)(deadline_ms / 1000), (long)(deadline_ms % 1000) * 1000000L};

        pthread_mutex_lock(&waiter->lock);
        while (!atomic_load(&waiter->signaled)) {
            if (deadline_ms == 0)
                pthread_cond_wait(&waiter->cond, &waiter->lock);
            else if (pthread_cond_timedwait(&waiter->cond, &waiter->lock, &deadline) != 0)
                break;
        }
        pthread_mutex_unlock(&waiter->lock);
    }

    atomic_store(&waiter->sleeping, false);
}

void idle_wake(IdleWaiter *waiter) {
    atomic_store(&waiter->signaled, true);
    if (atomic_load(&waiter->sleeping)) {
        pthread_mutex_lock(&waiter->lock);
        pthread_cond_signal(&waiter->cond);
        pthread_mutex_unlock(&waiter->lock);
    }
}
