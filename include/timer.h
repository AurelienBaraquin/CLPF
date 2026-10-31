#pragma once

#include <mailbox.h>
#include <pthread.h>
#include <stdatomic.h>
#include <stdint.h>

// Timers: when one is due, its message is delivered as if `from` had sent it.
// Each scheduler owns a queue of the timers armed by the processes it runs,
// fires them in its loop and sleeps until the next one. Thread-safe.
// A TimerId tells the queue, the slot and its generation: never 0, and an
// id whose timer fired or was cancelled never designates a later one.
typedef uint64_t TimerId;

typedef struct Timer Timer;

typedef struct {
    Timer *timer; // NULL while free
    uint32_t generation;
} TimerSlot;

typedef struct TimerQueue {
    pthread_mutex_t lock;
    int index; // Of its scheduler

    // Guarded by lock: a min-heap by deadline, and the slots ids point to
    Timer **heap;
    size_t count;
    size_t capacity;
    TimerSlot *slots;
    uint32_t slot_count;
    uint32_t *free_slots;
    uint32_t free_count;
    uint64_t sequence; // Orders equal deadlines by arming order

    _Atomic uint64_t next_deadline; // now_ms() of the earliest timer, 0 if none
} TimerQueue;

void timer_queue_init(TimerQueue *queue, int index);
void timer_queue_destroy(TimerQueue *queue);
// The queue's scheduler: delivers every due timer
void timer_queue_fire_due(TimerQueue *queue, uint64_t now);
// The queue timers armed from this thread go to (scheduler threads set theirs)
void timer_queue_set_current(TimerQueue *queue);

// Copies data and the tag string; returns the timer id, or 0 if out of memory
TimerId timer_add(Pid from, Pid to, uint64_t delay_ms, const void *data, size_t size, Tag tag);
// Fires every interval_ms (> 0, otherwise returns 0) until cancelled or until `to` is gone
TimerId timer_add_interval(Pid from, Pid to, uint64_t interval_ms, const void *data, size_t size, Tag tag);
// A timer whose message is MAKE_TIMEOUT_TAG(id), empty, sent to pid itself
TimerId timer_add_timeout(Pid pid, uint64_t delay_ms);
// Returns 0, or -1 if the timer already fired or was cancelled.
// A message of a timer that fired is in the receiver's inbox before this returns.
int timer_cancel(TimerId id);
