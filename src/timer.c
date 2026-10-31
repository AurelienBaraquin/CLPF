#include <timer.h>
#include <clpf_internal.h>
#include <utils/time.h>
#include <stdlib.h>
#include <string.h>

#define ID_QUEUE_SHIFT      56
#define ID_GENERATION_SHIFT 32
#define ID_GENERATION_MASK  0xFFFFFFu
#define ID_SLOT_MASK        0xFFFFFFFFu

struct Timer {
    uint32_t slot;
    size_t heap_index;
    uint64_t deadline;
    uint64_t sequence;
    Pid from;
    Pid to;
    Tag tag;    // a string tag points to a copy owned by the timer
    void *data; // owned copy
    size_t size;
    uint64_t interval_ms; // fires again every interval_ms until cancelled (0: once)
};

static _Thread_local TimerQueue *current_queue;

void timer_queue_set_current(TimerQueue *queue) {
    current_queue = queue;
}

static bool before(const Timer *a, const Timer *b) {
    return a->deadline < b->deadline || (a->deadline == b->deadline && a->sequence < b->sequence);
}

static void place(TimerQueue *queue, size_t i, Timer *timer) {
    queue->heap[i] = timer;
    timer->heap_index = i;
}

static void sift_up(TimerQueue *queue, size_t i) {
    Timer *timer = queue->heap[i];
    while (i > 0 && before(timer, queue->heap[(i - 1) / 2])) {
        place(queue, i, queue->heap[(i - 1) / 2]);
        i = (i - 1) / 2;
    }
    place(queue, i, timer);
}

static void sift_down(TimerQueue *queue, size_t i) {
    while (1) {
        size_t smallest = i, left = 2 * i + 1, right = left + 1;
        if (left < queue->count && before(queue->heap[left], queue->heap[smallest]))
            smallest = left;
        if (right < queue->count && before(queue->heap[right], queue->heap[smallest]))
            smallest = right;
        if (smallest == i)
            return;
        Timer *timer = queue->heap[i];
        place(queue, i, queue->heap[smallest]);
        place(queue, smallest, timer);
        i = smallest;
    }
}

static void publish_next(TimerQueue *queue) {
    atomic_store(&queue->next_deadline, queue->count > 0 ? queue->heap[0]->deadline : 0);
}

static void heap_remove(TimerQueue *queue, Timer *timer) {
    size_t i = timer->heap_index;
    Timer *last = queue->heap[--queue->count];
    if (i < queue->count) {
        place(queue, i, last);
        sift_down(queue, i);
        sift_up(queue, last->heap_index);
    }
}

// Takes the earliest timer out of the heap
static Timer *heap_pop(TimerQueue *queue) {
    Timer *earliest = queue->heap[0];
    Timer *last = queue->heap[--queue->count];
    if (queue->count > 0) {
        place(queue, 0, last);
        sift_down(queue, 0);
    }
    return earliest;
}

static void heap_push(TimerQueue *queue, Timer *timer) {
    place(queue, queue->count++, timer);
    sift_up(queue, timer->heap_index);
}

// The slot of a timer that left the queue: its id is spent
static void slot_release(TimerQueue *queue, uint32_t slot) {
    queue->slots[slot].timer = NULL;
    queue->slots[slot].generation = (queue->slots[slot].generation + 1) & ID_GENERATION_MASK;
    if (queue->slots[slot].generation == 0)
        queue->slots[slot].generation = 1;
    queue->free_slots[queue->free_count++] = slot;
}

static void timer_free(Timer *timer) {
    if (timer->tag.kind == TAG_KIND_STRING || timer->tag.kind == TAG_KIND_NOTIFY)
        free((char *)timer->tag.str_value);
    free(timer->data);
    free(timer);
}

void timer_queue_init(TimerQueue *queue, int index) {
    memset(queue, 0, sizeof *queue);
    pthread_mutex_init(&queue->lock, NULL);
    queue->index = index;
    atomic_init(&queue->next_deadline, 0);
}

void timer_queue_destroy(TimerQueue *queue) {
    for (size_t i = 0; i < queue->count; i++)
        timer_free(queue->heap[i]);
    free((void *)queue->heap);
    free(queue->slots);
    free(queue->free_slots);
    pthread_mutex_destroy(&queue->lock);
}

// Room for one more timer in the heap and the slots
static bool reserve(TimerQueue *queue) {
    if (queue->count == queue->capacity) {
        size_t capacity = queue->capacity ? queue->capacity * 2 : 64;
        Timer **heap = (Timer **)realloc((void *)queue->heap, capacity * sizeof(Timer *));
        if (!heap)
            return false;
        queue->heap = heap;
        queue->capacity = capacity;
    }
    if (queue->free_count == 0) {
        if (queue->slot_count > ID_SLOT_MASK / 2)
            return false;
        uint32_t count = queue->slot_count ? queue->slot_count * 2 : 64;
        TimerSlot *slots = realloc(queue->slots, count * sizeof(TimerSlot));
        if (!slots)
            return false;
        queue->slots = slots;
        uint32_t *free_slots = realloc(queue->free_slots, count * sizeof(uint32_t));
        if (!free_slots)
            return false;
        queue->free_slots = free_slots;
        for (uint32_t slot = count; slot > queue->slot_count; --slot) {
            queue->slots[slot - 1] = (TimerSlot){.timer = NULL, .generation = 1};
            queue->free_slots[queue->free_count++] = slot - 1;
        }
        queue->slot_count = count;
    }
    return queue->free_count > 0;
}

// The current thread's queue; another thread's queue must be woken up
static TimerQueue *owner_queue(bool *foreign) {
    *foreign = current_queue == NULL;
    return current_queue ? current_queue : &clpf.scheduler_table[0].timers;
}

static TimerId insert(Timer *timer, uint64_t delay_ms) {
    bool foreign = false;
    TimerQueue *queue = owner_queue(&foreign);

    pthread_mutex_lock(&queue->lock);
    if (!reserve(queue)) {
        pthread_mutex_unlock(&queue->lock);
        timer_free(timer);
        return 0;
    }
    timer->slot = queue->free_slots[--queue->free_count];
    timer->deadline = now_ms() + delay_ms;
    timer->sequence = ++queue->sequence;
    TimerId id = ((uint64_t)(queue->index + 1) << ID_QUEUE_SHIFT) |
                 ((uint64_t)queue->slots[timer->slot].generation << ID_GENERATION_SHIFT) | timer->slot;
    if (timer->tag.kind == TAG_KIND_TIMEOUT)
        timer->tag.int_value = (int64_t)id;
    queue->slots[timer->slot].timer = timer;
    heap_push(queue, timer);
    publish_next(queue);
    pthread_mutex_unlock(&queue->lock);

    // Its scheduler may be sleeping until a later deadline
    if (foreign)
        scheduler_wake(&clpf.scheduler_table[queue->index]);
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
    uint64_t queue_number = id >> ID_QUEUE_SHIFT;
    if (queue_number == 0 || queue_number > (uint64_t)clpf.scheduler_count)
        return -1;
    TimerQueue *queue = &clpf.scheduler_table[queue_number - 1].timers;
    uint32_t slot = (uint32_t)(id & ID_SLOT_MASK);
    uint32_t generation = (uint32_t)(id >> ID_GENERATION_SHIFT) & ID_GENERATION_MASK;

    Timer *timer = NULL;
    pthread_mutex_lock(&queue->lock);
    if (slot < queue->slot_count && queue->slots[slot].timer && queue->slots[slot].generation == generation) {
        timer = queue->slots[slot].timer;
        heap_remove(queue, timer);
        slot_release(queue, slot);
        publish_next(queue);
    }
    pthread_mutex_unlock(&queue->lock);

    if (!timer)
        return -1;
    timer_free(timer);
    return 0;
}

void timer_queue_fire_due(TimerQueue *queue, uint64_t now) {
    // Delivering under the lock: once timer_cancel fails, the message is already delivered
    pthread_mutex_lock(&queue->lock);
    while (queue->count > 0 && queue->heap[0]->deadline <= now) {
        Timer *timer = heap_pop(queue);
        bool delivered = clpf_deliver(timer->from, timer->to, timer->data, timer->size, timer->tag) == 0;

        // A periodic timer goes back in the heap, with the same id, unless its target is gone
        if (timer->interval_ms > 0 && delivered) {
            timer->deadline += timer->interval_ms;
            if (timer->deadline <= now) // late: skip the missed ticks instead of firing a burst
                timer->deadline = now + timer->interval_ms;
            heap_push(queue, timer);
        } else {
            slot_release(queue, timer->slot);
            timer_free(timer);
        }
    }
    publish_next(queue);
    pthread_mutex_unlock(&queue->lock);
}
