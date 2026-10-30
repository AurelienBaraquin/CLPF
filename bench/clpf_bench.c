// Measures the costs that matter when designing with CLPF processes.
// Usage: clpf_bench [--quick]   (--quick: 100 times less work, as a smoke test)
#include <api/task.h>
#include <api/message.h>
#include <api/io.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#define SPAWNS        100000
#define MESSAGES      1000000
#define ROUND_TRIPS   200000
#define WAITERS       10000
#define PAIRS         8
#define PAIR_MESSAGES 200000
#define QUICK_DIVISOR 100
// What a spawn loop charges itself per SPAWN, as SPAWN costs no reductions
#define SPAWN_REDUCTIONS 50

static int scale = 1;

static double now_s(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (double)ts.tv_sec + (double)ts.tv_nsec / 1e9;
}

// Resident memory of the whole program, in MiB
static double resident_mib(void) {
    char line[128] = {0};
    FILE *statm = fopen("/proc/self/statm", "r");
    if (statm) {
        if (!fgets(line, sizeof line, statm))
            line[0] = '\0';
        fclose(statm);
    }
    // The second field: resident pages
    char *resident = strchr(line, ' ');
    long pages = resident ? strtol(resident + 1, NULL, 10) : 0;
    return (double)pages * (double)sysconf(_SC_PAGESIZE) / (1024.0 * 1024.0);
}

TASK(notify_and_exit, Pid boss) {
    SEND(boss, NULL, 0, MAKE_STR_TAG("done"));
}

TASK(counter, Pid boss, int expected) {
    for (int i = 0; i < expected; ++i)
        MSG_FREE(RECEIVE(MAKE_STR_TAG("count")));
    SEND(boss, NULL, 0, MAKE_STR_TAG("done"));
}

TASK(echo, int round_trips) {
    for (int i = 0; i < round_trips; ++i) {
        Message *msg = RECEIVE(MAKE_STR_TAG("ping"));
        SEND(msg->sender_pid, NULL, 0, MAKE_STR_TAG("pong"));
        MSG_FREE(msg);
    }
}

TASK(waiter, Pid boss) {
    MSG_FREE(RECEIVE(MAKE_STR_TAG("wake")));
    SEND(boss, NULL, 0, MAKE_STR_TAG("done"));
}

TASK(pinger, Pid boss, int round_trips) {
    Pid partner = SPAWN(echo, 1, round_trips);
    for (int i = 0; i < round_trips; ++i) {
        SEND(partner, NULL, 0, MAKE_STR_TAG("ping"));
        MSG_FREE(RECEIVE(MAKE_STR_TAG("pong")));
    }
    SEND(boss, NULL, 0, MAKE_STR_TAG("done"));
}

static void collect(Process *self, int count) {
    for (int i = 0; i < count; ++i)
        MSG_FREE(RECEIVE(MAKE_STR_TAG("done")));
}

static void report(Process *self, const char *name, double value, const char *unit) {
    PRINT("%-30s %14.2f %s\n", name, value, unit);
}

static void bench_spawn(Process *self) {
    int count = SPAWNS / scale;
    double start = now_s();
    for (int i = 0; i < count; ++i) {
        SPAWN(notify_and_exit, 1, self->pid);
        REDUCE(SPAWN_REDUCTIONS); // SPAWN never yields: let the children run
    }
    collect(self, count);
    report(self, "spawn + exit", count / (now_s() - start), "processes/s");
}

static void bench_throughput(Process *self) {
    int count = MESSAGES / scale;
    Pid receiver = SPAWN(counter, 2, self->pid, count);
    int value = 0;
    double start = now_s();
    for (int i = 0; i < count; ++i)
        SEND(receiver, &value, sizeof value, MAKE_STR_TAG("count"));
    collect(self, 1);
    report(self, "one-way messages", count / (now_s() - start), "messages/s");
}

static void bench_latency(Process *self) {
    int count = ROUND_TRIPS / scale;
    Pid partner = SPAWN(echo, 1, count);
    double start = now_s();
    for (int i = 0; i < count; ++i) {
        SEND(partner, NULL, 0, MAKE_STR_TAG("ping"));
        MSG_FREE(RECEIVE(MAKE_STR_TAG("pong")));
    }
    report(self, "ping-pong round trip", (now_s() - start) / count * 1e6, "us");
}

static void bench_waiters(Process *self) {
    int count = WAITERS / (scale > 1 ? 10 : 1);
    static Pid pids[WAITERS];
    double memory_before = resident_mib();
    double start = now_s();
    for (int i = 0; i < count; ++i) {
        pids[i] = SPAWN(waiter, 1, self->pid);
        REDUCE(SPAWN_REDUCTIONS);
    }
    double spawned = now_s();
    report(self, "spawn of waiting processes", (spawned - start) / count * 1e6, "us each");
    report(self, "memory per waiting process", (resident_mib() - memory_before) * 1024.0 / count, "KiB");

    start = now_s();
    for (int i = 0; i < count; ++i)
        SEND(pids[i], NULL, 0, MAKE_STR_TAG("wake"));
    collect(self, count);
    report(self, "wake and collect all", (now_s() - start) * 1e3, "ms");
}

static void bench_parallel(Process *self) {
    int round_trips = PAIR_MESSAGES / scale;
    double start = now_s();
    for (int i = 0; i < PAIRS; ++i)
        SPAWN(pinger, 2, self->pid, round_trips);
    collect(self, PAIRS);
    report(self, "parallel ping-pong (8 pairs)", PAIRS * round_trips / (now_s() - start), "round trips/s");
}

TASK(entry, int argc, char *argv[]) {
    if (argc > 1 && strcmp(argv[1], "--quick") == 0)
        scale = QUICK_DIVISOR;
    PRINT("CLPF benchmarks%s, %ld cores\n\n", scale > 1 ? " (quick)" : "", sysconf(_SC_NPROCESSORS_ONLN));

    bench_spawn(self);
    bench_throughput(self);
    bench_latency(self);
    bench_waiters(self);
    bench_parallel(self);
}

CLPF_ENTRY(entry)
