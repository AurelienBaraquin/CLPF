#include "test_helpers.h"
#include <api/task.h>
#include <api/message.h>
#include <api/memory.h>
#include <utils/time.h>
#include <pthread.h>
#include <sys/resource.h>
#include <time.h>

#define SETTLE_MS       100
#define WINDOW_MS       500
#define MAX_IDLE_CPU_US 10000 // 2% of a core over the window

#ifdef CLPF_SANITIZED
#define ROUND_TRIPS 2000
#else
#define ROUND_TRIPS 20000
#endif
#define MAX_ROUND_TRIPS_MS 2000

static _Atomic Pid entry_pid = INVALID_PID;

static void sleep_ms(long ms) {
    struct timespec ts = {ms / 1000, (ms % 1000) * 1000000L};
    nanosleep(&ts, NULL);
}

static long cpu_time_us(void) {
    struct rusage usage;
    getrusage(RUSAGE_SELF, &usage);
    return (usage.ru_utime.tv_sec + usage.ru_stime.tv_sec) * 1000000L + usage.ru_utime.tv_usec + usage.ru_stime.tv_usec;
}

// Runs outside the runtime: measures the CPU used while every process is blocked
static void *idle_probe(void *arg) {
    (void)arg;
    while (atomic_load(&entry_pid) == INVALID_PID)
        sleep_ms(1);

    sleep_ms(SETTLE_MS);
    long start = cpu_time_us();
    sleep_ms(WINDOW_MS);
    long used = cpu_time_us() - start;

    clpf_send(NULL, atomic_load(&entry_pid), &used, sizeof used, MAKE_STR_TAG("cpu"));
    return NULL;
}

TASK0(echo) {
    for (int i = 0; i < ROUND_TRIPS; ++i) {
        Message *msg = RECEIVE(MAKE_STR_TAG("ping"));
        SEND(msg->sender_pid, NULL, 0, MAKE_STR_TAG("pong"));
        MSG_FREE(msg);
    }
    EXIT_NORMAL();
}

TASK(entry, int argc, char *argv[]) {
    (void)argc;
    (void)argv;

    // An idle runtime sleeps instead of polling
    atomic_store(&entry_pid, self->pid);
    Message *msg = RECEIVE(MAKE_STR_TAG("cpu"));
    long used = *(long *)msg->payload;
    MSG_FREE(msg);
    fprintf(stderr, "idle runtime used %ld us of CPU in %d ms\n", used, WINDOW_MS);
    TEST_ASSERT(used < MAX_IDLE_CPU_US);

    // ...and a sleeping scheduler wakes up as soon as a message reaches it
    Pid echo_pid = SPAWN0(echo);

    uint64_t start = now_ms();
    for (int i = 0; i < ROUND_TRIPS; ++i) {
        SEND(echo_pid, NULL, 0, MAKE_STR_TAG("ping"));
        MSG_FREE(RECEIVE(MAKE_STR_TAG("pong")));
    }
    uint64_t elapsed = now_ms() - start;
    fprintf(stderr, "%d cross-scheduler round trips in %lu ms\n", ROUND_TRIPS, (unsigned long)elapsed);
    TEST_ASSERT(elapsed < MAX_ROUND_TRIPS_MS);

    TEST_PASS();
    EXIT_NORMAL();
}

_Atomic TestStatus test_status = TEST_RUNNING;

int main(int argc, char *argv[]) {
    pthread_t probe;
    pthread_create(&probe, NULL, idle_probe, NULL);
    clpf_main(entry, argc, argv);
    pthread_join(probe, NULL);
    return atomic_load(&test_status) == TEST_PASSED ? 0 : 1;
}
