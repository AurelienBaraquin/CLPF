#include <watchdog.h>
#include <clpf_internal.h>
#include <stdio.h>
#include <stdlib.h>
#include <utils/time.h>

#define WATCHDOG_DEFAULT_MS 1000

static uint64_t limit_ms = WATCHDOG_DEFAULT_MS;
// Set by the main thread when it sleeps without a deadline: the next resume wakes it
static atomic_bool wake_on_run;
// Main thread only: when the last checked run started, per scheduler, so each one is reported once
static uint64_t reported[MAX_SCHEDULERS];
static uint64_t quiet_until;

void watchdog_init(void) {
    const char *value = getenv("CLPF_WATCHDOG_MS");
    if (!value)
        return;
    char *end = NULL;
    unsigned long long parsed = strtoull(value, &end, 10);
    if (end == value || *end != '\0') {
        fprintf(stderr, "CLPF_WATCHDOG_MS: \"%s\" is not a number of milliseconds, using %d\n", value,
                WATCHDOG_DEFAULT_MS);
        return;
    }
    limit_ms = parsed;
}

void watchdog_enter(Scheduler *scheduler, Process *process) {
    if (!limit_ms)
        return;
    atomic_store(&scheduler->running_pid, process->pid);
    atomic_store(&scheduler->running_since, now_ms());
    // Pairs with watchdog_check: either it sees this run, or this sees its request
    if (atomic_load(&wake_on_run) && atomic_exchange(&wake_on_run, false))
        idle_wake(&clpf.main_idle);
}

void watchdog_leave(Scheduler *scheduler) {
    if (limit_ms)
        atomic_store(&scheduler->running_since, 0);
}

static void report(int index, Pid pid, uint64_t running_ms) {
    // Only the main thread destroys processes: the slot cannot change under us
    Process *process = &clpf.process_table[PID_INDEX(pid)];
    fprintf(stderr,
            "clpf: watchdog: process '%s' (pid %ld) has run for %lu ms on scheduler %d without yielding; "
            "call REDUCE in long loops\n",
            process->name, pid, (unsigned long)running_ms, index);
}

static bool check_schedulers(uint64_t now) {
    bool running = false;
    for (int i = 0; i < clpf.scheduler_count; ++i) {
        Scheduler *scheduler = &clpf.scheduler_table[i];
        uint64_t since = atomic_load(&scheduler->running_since);
        if (!since)
            continue;
        running = true;
        // A process resumed after now was read has not run long: now - since would wrap around
        if (since <= now && now - since >= limit_ms && reported[i] != since) {
            reported[i] = since;
            report(i, atomic_load(&scheduler->running_pid), now - since);
        }
    }
    return running;
}

uint64_t watchdog_check(uint64_t now) {
    if (!limit_ms)
        return 0;

    // Checked twice as often as the limit: a long run is reported within 1.5 limits
    uint64_t period = limit_ms > 1 ? limit_ms / 2 : 1;
    if (check_schedulers(now))
        quiet_until = now + period;
    if (now < quiet_until)
        return now + period;

    // Nothing ran for a period: sleep until a process runs, unless one just started
    atomic_store(&wake_on_run, true);
    if (check_schedulers(now)) {
        atomic_store(&wake_on_run, false);
        quiet_until = now + period;
        return now + period;
    }
    return 0;
}
