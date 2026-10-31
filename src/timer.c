#include <timer.h>
#include <clpf_internal.h>
#include <utils/time.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
    TimerId id;
    uint64_t deadline;
    Pid from;
    Pid to;
    Tag tag;    // a string tag points to a copy owned by the timer
    void *data; // owned copy
    size_t size;
    uint64_t interval_ms; // fires again every interval_ms until cancelled (0: once)
} Timer;

// Min-heap ordered by deadline, then id
static struct {
    pthread_mutex_t lock;
    Timer **heap;
    size_t count;
    size_t capacity;
    TimerId last_id;
} timers;

static bool before(const Timer *a, const Timer *b) {
    return a->deadline < b->deadline || (a->deadline == b->deadline && a->id < b->id);
}

static void swap(size_t i, size_t j) {
    Timer *tmp = timers.heap[i];
    timers.heap[i] = timers.heap[j];
    timers.heap[j] = tmp;
}

static void sift_up(size_t i) {
    while (i > 0 && before(timers.heap[i], timers.heap[(i - 1) / 2])) {
        swap(i, (i - 1) / 2);
        i = (i - 1) / 2;
    }
}

static void sift_down(size_t i) {
    while (1) {
        size_t smallest = i, left = 2 * i + 1, right = left + 1;
        if (left < timers.count && before(timers.heap[left], timers.heap[smallest]))
            smallest = left;
        if (right < timers.count && before(timers.heap[right], timers.heap[smallest]))
            smallest = right;
        if (smallest == i)
            return;
        swap(i, smallest);
        i = smallest;
    }
}

static Timer *remove_at(size_t i) {
    Timer *timer = timers.heap[i];
    timers.heap[i] = timers.heap[--timers.count];
    if (i < timers.count) {
        sift_down(i);
        sift_up(i);
    }
    return timer;
}

static void timer_free(Timer *timer) {
    if (timer->tag.kind == TAG_KIND_STRING || timer->tag.kind == TAG_KIND_NOTIFY)
        free((char *)timer->tag.str_value);
    free(timer->data);
    free(timer);
}

void timers_init(void) {
    pthread_mutex_init(&timers.lock, NULL);
    timers.heap = NULL;
    timers.count = 0;
    timers.capacity = 0;
    timers.last_id = 0;
}

void timers_destroy(void) {
    for (size_t i = 0; i < timers.count; i++)
        timer_free(timers.heap[i]);
    free((void *)timers.heap);
    pthread_mutex_destroy(&timers.lock);
}

static TimerId insert(Timer *timer, uint64_t delay_ms) {
    pthread_mutex_lock(&timers.lock);
    if (timers.count == timers.capacity) {
        size_t capacity = timers.capacity ? timers.capacity * 2 : 64;
        Timer **heap = (Timer **)realloc((void *)timers.heap, capacity * sizeof(Timer *));
        if (!heap) {
            pthread_mutex_unlock(&timers.lock);
            timer_free(timer);
            return 0;
        }
        timers.heap = heap;
        timers.capacity = capacity;
    }

    timer->id = ++timers.last_id;
    timer->deadline = now_ms() + delay_ms;
    if (timer->tag.kind == TAG_KIND_TIMEOUT)
        timer->tag.int_value = (int64_t)timer->id;
    timers.heap[timers.count] = timer;
    sift_up(timers.count++);
    TimerId id = timer->id;
    pthread_mutex_unlock(&timers.lock);

    // The main thread may be sleeping until a later deadline
    clpf_wake_main();
    return id;
}

// A timer holding copies of data and of the tag string, or NULL if out of memory
static Timer *timer_new(Pid from, Pid to, const void *data, size_t size, Tag tag) {
    Timer *timer = calloc(1, sizeof(Timer));
    if (!timer)
        return NULL;

    timer->from = from;
    timer->to = to;
    timer->tag = tag;
    if (tag.kind == TAG_KIND_STRING || tag.kind == TAG_KIND_NOTIFY) {
        timer->tag.str_value = strdup(tag.str_value);
        if (!timer->tag.str_value) {
            free(timer);
            return NULL;
        }
    }
    if (data && size > 0) {
        timer->data = malloc(size);
        if (!timer->data) {
            timer_free(timer);
            return NULL;
        }
        memcpy(timer->data, data, size);
        timer->size = size;
    }
    return timer;
}

TimerId timer_add(Pid from, Pid to, uint64_t delay_ms, const void *data, size_t size, Tag tag) {
    Timer *timer = timer_new(from, to, data, size, tag);
    return timer ? insert(timer, delay_ms) : 0;
}

TimerId timer_add_interval(Pid from, Pid to, uint64_t interval_ms, const void *data, size_t size, Tag tag) {
    if (interval_ms == 0)
        return 0;
    Timer *timer = timer_new(from, to, data, size, tag);
    if (!timer)
        return 0;
    timer->interval_ms = interval_ms;
    return insert(timer, interval_ms);
}

TimerId timer_add_timeout(Pid pid, uint64_t delay_ms) {
    Timer *timer = calloc(1, sizeof(Timer));
    if (!timer)
        return 0;

    timer->from = pid;
    timer->to = pid;
    timer->tag = MAKE_TIMEOUT_TAG(0); // the id is filled in once known
    return insert(timer, delay_ms);
}

int timer_cancel(TimerId id) {
    int result = -1;

    pthread_mutex_lock(&timers.lock);
    for (size_t i = 0; i < timers.count; i++) {
        if (timers.heap[i]->id == id) {
            timer_free(remove_at(i));
            result = 0;
            break;
        }
    }
    pthread_mutex_unlock(&timers.lock);
    return result;
}

uint64_t timers_fire_due(void) {
    uint64_t now = now_ms();

    // Delivering under the lock: once timer_cancel fails, the message is already delivered
    pthread_mutex_lock(&timers.lock);
    while (timers.count > 0 && timers.heap[0]->deadline <= now) {
        Timer *timer = remove_at(0);
        bool delivered = clpf_deliver(timer->from, timer->to, timer->data, timer->size, timer->tag) == 0;

        // A periodic timer goes back in the heap (whose slot it just left), unless its target is gone
        if (timer->interval_ms > 0 && delivered) {
            timer->deadline += timer->interval_ms;
            if (timer->deadline <= now) // late: skip the missed ticks instead of firing a burst
                timer->deadline = now + timer->interval_ms;
            timers.heap[timers.count] = timer;
            sift_up(timers.count++);
        } else {
            timer_free(timer);
        }
    }
    uint64_t next = timers.count > 0 ? timers.heap[0]->deadline : 0;
    pthread_mutex_unlock(&timers.lock);
    return next;
}
