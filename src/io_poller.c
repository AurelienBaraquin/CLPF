#include <io_poller.h>
#include <clpf_internal.h>
#include <errno.h>
#include <stdlib.h>
#include <sys/epoll.h>
#include <unistd.h>

#define POLLER_BATCH 64

// One slot per fd. seq tells a stale epoll event (from a watch that was
// cancelled or replaced since) from the current one: both travel in epoll_data.
typedef struct {
    Pid pid; // INVALID_PID when nobody watches the fd
    uint32_t seq;
} Watch;

// Never destroyed: the thread may still be blocked in epoll_wait when the program ends
static int epoll_fd = -1;
static pthread_once_t started = PTHREAD_ONCE_INIT;
static pthread_mutex_t lock = PTHREAD_MUTEX_INITIALIZER;
static Watch *watches; // Guarded by lock, as everything below
static int capacity;
static int active;
static int watched_end; // 1 + the highest fd ever watched: scans stop there

static uint32_t to_epoll(int events) {
    return (events & IO_READ ? EPOLLIN | EPOLLRDHUP : 0) | (events & IO_WRITE ? EPOLLOUT : 0) | EPOLLONESHOT;
}

static int from_epoll(uint32_t events) {
    return (events & EPOLLIN ? IO_READ : 0) | (events & EPOLLOUT ? IO_WRITE : 0) | (events & EPOLLERR ? IO_ERROR : 0) |
           (events & (EPOLLHUP | EPOLLRDHUP) ? IO_HANGUP : 0);
}

// Takes the watch of a fired event if it is still current, and removes the fd
// from epoll so that a later watch starts from a clean state
static Pid take_fired(uint64_t data) {
    int fd = (int)(uint32_t)data;
    uint32_t seq = (uint32_t)(data >> 32);
    Pid pid = INVALID_PID;

    pthread_mutex_lock(&lock);
    if (fd < capacity && watches[fd].pid != INVALID_PID && watches[fd].seq == seq) {
        pid = watches[fd].pid;
        watches[fd].pid = INVALID_PID;
        --active;
        epoll_ctl(epoll_fd, EPOLL_CTL_DEL, fd, NULL);
    }
    pthread_mutex_unlock(&lock);
    return pid;
}

static void *poller_thread(void *arg) {
    (void)arg;
    struct epoll_event events[POLLER_BATCH];

    while (1) {
        int count = epoll_wait(epoll_fd, events, POLLER_BATCH, -1);
        for (int i = 0; i < count; ++i) {
            Pid pid = take_fired(events[i].data.u64);
            if (pid == INVALID_PID)
                continue;
            int ready = from_epoll(events[i].events);
            int fd = (int)(uint32_t)events[i].data.u64;
            // Fails only if the watcher is gone: nothing to do then
            clpf_deliver(INVALID_PID, pid, &ready, sizeof ready, MAKE_FD_TAG(fd));
        }
    }
    return NULL;
}

static void start_poller(void) {
    epoll_fd = epoll_create1(EPOLL_CLOEXEC);
    if (epoll_fd < 0)
        return;

    pthread_t thread;
    if (pthread_create(&thread, NULL, poller_thread, NULL) != 0) {
        close(epoll_fd);
        epoll_fd = -1;
        return;
    }
    pthread_detach(thread);
}

static int ensure_capacity(int fd) {
    if (fd < capacity)
        return 0;

    int new_capacity = capacity ? capacity : 64;
    while (new_capacity <= fd)
        new_capacity *= 2;
    Watch *grown = realloc(watches, (size_t)new_capacity * sizeof(Watch));
    if (!grown)
        return -1;
    for (int i = capacity; i < new_capacity; ++i)
        grown[i] = (Watch){.pid = INVALID_PID, .seq = 0};
    watches = grown;
    capacity = new_capacity;
    return 0;
}

// The table and epoll may disagree when the fd was closed while watched:
// the kernel then forgot it. Fall back to the other operation in that case.
static int register_fd(int fd, bool known, struct epoll_event *event) {
    if (epoll_ctl(epoll_fd, known ? EPOLL_CTL_MOD : EPOLL_CTL_ADD, fd, event) == 0)
        return 0;
    if ((known && errno == ENOENT) || (!known && errno == EEXIST))
        return epoll_ctl(epoll_fd, known ? EPOLL_CTL_ADD : EPOLL_CTL_MOD, fd, event);
    return -1;
}

int io_poller_watch(Pid pid, int fd, int events) {
    if (fd < 0 || !(events & (IO_READ | IO_WRITE)))
        return -1;
    pthread_once(&started, start_poller);
    if (epoll_fd < 0)
        return -1;

    int result = -1;
    pthread_mutex_lock(&lock);
    if (ensure_capacity(fd) == 0 && (watches[fd].pid == INVALID_PID || watches[fd].pid == pid)) {
        Watch *watch = &watches[fd];
        bool known = watch->pid != INVALID_PID;
        ++watch->seq;
        struct epoll_event event = {.events = to_epoll(events),
                                    .data.u64 = (uint32_t)fd | ((uint64_t)watch->seq << 32)};

        if (register_fd(fd, known, &event) == 0) {
            if (!known)
                ++active;
            if (fd >= watched_end)
                watched_end = fd + 1;
            watch->pid = pid;
            result = 0;
        } else if (known) {
            watch->pid = INVALID_PID;
            --active;
        }
    }
    pthread_mutex_unlock(&lock);
    return result;
}

int io_poller_unwatch(Pid pid, int fd) {
    int result = -1;

    pthread_mutex_lock(&lock);
    if (fd >= 0 && fd < capacity && watches[fd].pid == pid) {
        watches[fd].pid = INVALID_PID;
        --active;
        epoll_ctl(epoll_fd, EPOLL_CTL_DEL, fd, NULL);
        result = 0;
    }
    pthread_mutex_unlock(&lock);
    return result;
}

void io_poller_forget(Pid pid) {
    pthread_mutex_lock(&lock);
    for (int fd = 0; active > 0 && fd < watched_end; ++fd) {
        if (watches[fd].pid != pid)
            continue;
        watches[fd].pid = INVALID_PID;
        --active;
        epoll_ctl(epoll_fd, EPOLL_CTL_DEL, fd, NULL);
    }
    pthread_mutex_unlock(&lock);
}
