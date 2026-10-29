#include <utils/idle.h>

void idle_init(IdleWaiter *waiter) {
    pthread_mutex_init(&waiter->lock, NULL);
    pthread_cond_init(&waiter->cond, NULL);
    atomic_init(&waiter->sleeping, false);
    atomic_init(&waiter->signaled, false);
}

void idle_destroy(IdleWaiter *waiter) {
    pthread_mutex_destroy(&waiter->lock);
    pthread_cond_destroy(&waiter->cond);
}

void idle_wait(IdleWaiter *waiter, bool (*has_work)(void *arg), void *arg) {
    atomic_store(&waiter->sleeping, true);
    atomic_store(&waiter->signaled, false);

    // Work published before this point is seen here; work published later
    // comes with an idle_wake that finds sleeping set
    if (!has_work(arg)) {
        pthread_mutex_lock(&waiter->lock);
        while (!atomic_load(&waiter->signaled))
            pthread_cond_wait(&waiter->cond, &waiter->lock);
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
