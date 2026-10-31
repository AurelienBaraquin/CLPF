#include <api/introspection.h>
#include <signal.h>
#include <string.h>

static const char *const STATE_NAMES[] = {"init", "running", "ready", "waiting", "exiting"};
static const char *const PRIORITY_NAMES[] = {"medium", "low", "high", "very_high"};

const char *clpf_state_name(process_state state) {
    return state <= PROCESS_EXITING ? STATE_NAMES[state] : "?";
}

const char *clpf_priority_name(process_priority priority) {
    return priority <= PROCESS_PRIORITY_VERY_HIGH ? PRIORITY_NAMES[priority] : "?";
}

int clpf_process_info(Pid pid, ProcessInfo *info) {
    if (pid < 0 || !info)
        return -1;

    // Same order as REGISTER: the table lock, then the slot lock
    pthread_mutex_lock(&clpf.table_lock);
    Process *process = process_lock(pid);
    if (!process) {
        pthread_mutex_unlock(&clpf.table_lock);
        return -1;
    }

    *info = (ProcessInfo){
        .pid = pid,
        .state = atomic_load(&process->state),
        .priority = process->priority,
        .scheduler = (int)atomic_load(&process->scheduler_pid),
        .queued = atomic_load(&process->queued),
        .heap_bytes = safe_heap_size(&process->heap),
        .runs = atomic_load_explicit(&process->runs, memory_order_relaxed),
        .links = link_count(process->linked),
        .children = process->links.child_count,
        .monitored_by = link_count(process->monitor.monitored_by),
        .trap_exit = process->trap_exit,
        .system = process->system,
    };
    memcpy(info->name, process->name, sizeof info->name);
    memcpy(info->registered_name, process->registered_name, sizeof info->registered_name);

    process_unlock(process);
    pthread_mutex_unlock(&clpf.table_lock);
    return 0;
}

size_t clpf_process_list(Pid *buffer, size_t max) {
    size_t count = 0;

    int end = clpf_slot_end();
    for (int i = 1; i < end; ++i) {
        Process *process = &clpf.process_table[i];
        pthread_mutex_lock(&process->lock);
        Pid pid = process->pid;
        pthread_mutex_unlock(&process->lock);

        if (pid == PID_FREE)
            continue;
        if (buffer && count < max)
            buffer[count] = pid;
        ++count;
    }
    return count;
}

void clpf_dump(FILE *out) {
    fprintf(out, "%-10s %-8s %-9s %5s %7s %10s %10s  %s\n", "PID", "STATE", "PRIORITY", "SCHED", "QUEUED", "HEAP",
            "RUNS", "NAME (REGISTERED)");

    size_t count = 0;
    int end = clpf_slot_end();
    for (int i = 1; i < end; ++i) {
        Process *process = &clpf.process_table[i];
        pthread_mutex_lock(&process->lock);
        Pid pid = process->pid;
        pthread_mutex_unlock(&process->lock);

        ProcessInfo info;
        if (pid == PID_FREE || clpf_process_info(pid, &info) != 0)
            continue;
        fprintf(out, "%-10ld %-8s %-9s %5d %7zu %10zu %10lu  %s", info.pid, clpf_state_name(info.state),
                clpf_priority_name(info.priority), info.scheduler, info.queued, info.heap_bytes,
                (unsigned long)info.runs, info.name[0] ? info.name : "-");
        if (info.registered_name[0])
            fprintf(out, " (%s)", info.registered_name);
        fputc('\n', out);
        ++count;
    }
    fprintf(out, "%zu processes\n", count);
    fflush(out);
}

static sigset_t dump_signals;
static pthread_t dump_thread_id;
static bool dump_thread_started;
static atomic_bool dump_stopping;

// Waits for SIGUSR1 outside any signal handler, so the dump may take locks
static void *dump_thread(void *arg) {
    (void)arg;
    while (1) {
        int signal = 0;
        if (sigwait(&dump_signals, &signal) != 0)
            continue;
        if (atomic_load(&dump_stopping))
            break;
        clpf_dump(stderr);
    }
    return NULL;
}

void clpf_dump_signal_init(void) {
    sigemptyset(&dump_signals);
    sigaddset(&dump_signals, SIGUSR1);
    // Every thread started from here on inherits the mask: the signal only reaches sigwait
    if (pthread_sigmask(SIG_BLOCK, &dump_signals, NULL) != 0)
        return;

    dump_thread_started = pthread_create(&dump_thread_id, NULL, dump_thread, NULL) == 0;
}

void clpf_dump_signal_stop(void) {
    if (!dump_thread_started)
        return;
    // A dump in progress reads the process table: it must end before the table is freed
    atomic_store(&dump_stopping, true);
    pthread_kill(dump_thread_id, SIGUSR1);
    pthread_join(dump_thread_id, NULL);
    dump_thread_started = false;
}
