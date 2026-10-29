// CLPF showcase: a supervised worker pool computing jobs.
//
//   entry (coordinator)
//    |- stats    : counter server, registered by name, answers REQUESTs (HIGH priority)
//    |- ticker   : LOW priority background process, polls with FETCH
//    |- echo     : replies to int-tagged requests, used for selective receive
//    `- pool     : ONE_FOR_ONE supervisor
//        |- worker x3 : RES_TRANSIENT, crash on "poison" jobs and get restarted
//        |- janitor   : RES_TEMPORARY, crashes once and is never restarted
//        `- spare     : RES_PERMANENT, removed from supervision then stopped
//
// Run with -i to be asked your name through GETLINE.

#include <api/base.h>
#include <api/task.h>
#include <api/message.h>
#include <api/memory.h>
#include <api/io.h>
#include <api/registry.h>
#include <api/monitor.h>
#include <api/supervisor.h>
#include <string.h>

#define WORKERS 3
#define JOBS 100
#define POISON_INTERVAL 37      // every 37th job crashes its worker
#define MAX_FIB 90              // fib(n) fits in a long up to n = 92
#define POOL_PERIOD_MS 1000
// A worker crashing more often than this within POOL_PERIOD_MS is given up by the pool,
// and nothing tells the coordinator: keep crashes rare enough or the demo waits forever
#define POOL_MAX_RESTARTS 5
#define ECHO_REQUESTS 3
#define TICKER_STOP 1           // internal tag code

typedef struct {
    int id;
    int value;
} Job;

typedef struct {
    int job_id;
    long result;
} JobResult;

typedef struct {
    Pid pid;
    int job_id; // -1 when idle
} WorkerSlot;

typedef struct {
    WorkerSlot workers[WORKERS * (POOL_MAX_RESTARTS + 1)];
    int known_workers;
    int stopped_workers;
    int next_job;
    int done;
    int failed;
    long sum;
} Coordinator;

static const char *reason_name(unsigned int reason) {
    switch (reason) {
        case EXIT_REASON_NORMAL: return "normal";
        case EXIT_REASON_ERROR:  return "error";
        case EXIT_REASON_NOPROC: return "noproc";
        default:                 return "unknown";
    }
}

/* ------------------------------------------------------------------ stats */

// Keeps a running total: "stats::add" adds and answers the new total, "stats::get" only answers
TASK0(stats) {
    long total = 0;

    while (1) {
        Message *msg = RECEIVE(NO_TAG);
        bool stop = MSG_IS(msg, MAKE_STR_TAG("stats::stop"));

        if (MSG_IS(msg, MAKE_STR_TAG("stats::add"))) {
            total += *(long *)msg->payload;
            SEND(msg->sender_pid, &total, sizeof total, MAKE_STR_TAG("stats::add"));
        } else if (MSG_IS(msg, MAKE_STR_TAG("stats::get"))) {
            SEND(msg->sender_pid, &total, sizeof total, MAKE_STR_TAG("stats::get"));
        }

        MSG_FREE(msg);

        if (stop)
            break;
    }

    PRINT("[stats] final total %ld\n", total);
    EXIT_NORMAL();
}

/* ----------------------------------------------------------------- ticker */

// Low priority: it only runs when nobody else needs the CPU, and polls for its stop message
TASK0(ticker) {
    PRINT("[ticker] started by %ld (my parent)\n", PARENT());
    NOTIFY(PARENT(), "ticker_up");

    // FETCH returns NULL only when no message matches
    long ticks = 0;
    Message *stop;
    while ((stop = FETCH(MAKE_INTERNAL_TAG(TICKER_STOP))) == NULL) {
        ++ticks;
        YIELD();
    }
    MSG_FREE(stop);

    SEND(PARENT(), &ticks, sizeof ticks, MAKE_STR_TAG("ticker::ticks"));
    EXIT_NORMAL();
}

/* ------------------------------------------------------------------- echo */

TASK0(echo) {
    for (int i = 0; i < ECHO_REQUESTS; ++i) {
        Message *msg = RECEIVE(NO_TAG);
        SEND(msg->sender_pid, msg->payload, msg->size, msg->tag);
        MSG_FREE(msg);
    }
    EXIT_NORMAL();
}

/* ----------------------------------------------------------------- worker */

static long DEF(fibonacci, int n) {
    long a = 0, b = 1;
    for (int i = 0; i < n; ++i) {
        long next = a + b;
        a = b;
        b = next;
        if (i % 10 == 0)
            YIELD(); // cooperative: let the other processes of this scheduler run
    }
    return a;
}

// Asks the coordinator for work until told to stop; a poison job crashes it
TASK(worker, Pid coordinator, int index) {
    Pid stats_pid = WHEREIS("stats");

    while (1) {
        SEND(coordinator, NULL, 0, MAKE_STR_TAG("worker::ready"));

        Message *msg = RECEIVE(NO_TAG);
        if (MSG_IS(msg, MAKE_STR_TAG("worker::stop"))) {
            MSG_FREE(msg);
            break;
        }

        Job job = *(Job *)msg->payload;
        MSG_FREE(msg);

        if ((job.id + 1) % POISON_INTERVAL == 0) {
            PRINT("[worker %d] job %d is poison, crashing\n", index, job.id);
            EXIT(EXIT_REASON_ERROR);
        }

        JobResult result = { job.id, CALL(fibonacci, job.value) };
        MSG_FREE(REQUEST(stats_pid, &result.result, sizeof result.result, MAKE_STR_TAG("stats::add")));

        SEND(coordinator, &result, sizeof result, MAKE_STR_TAG("worker::result"));
    }

    EXIT_NORMAL();
}

/* ------------------------------------------------- janitor / spare child */

TASK(janitor, Pid coordinator) {
    PRINT("[janitor] one-shot cleanup, then crash (temporary: not restarted)\n");
    NOTIFY(coordinator, "janitor_ran");
    EXIT_ERROR();
}

TASK0(spare) {
    MSG_FREE(RECEIVE(MAKE_STR_TAG("spare::stop")));
    PRINT("[spare] stopped after leaving the supervisor\n");
    EXIT_NORMAL();
}

/* ------------------------------------------------------------ coordinator */

static WorkerSlot *find_worker(Coordinator *c, Pid pid) {
    for (int i = 0; i < c->known_workers; ++i) {
        if (c->workers[i].pid == pid)
            return &c->workers[i];
    }
    return NULL;
}

// A crashed worker loses its job (the pool restarts it), a stopped one is done
static void DEF(on_worker_down, Coordinator *c, DownInfo *down) {
    WorkerSlot *slot = find_worker(c, down->from);
    if (!slot)
        return;

    PRINT("[coordinator] worker %ld down (%s)\n", down->from, reason_name(down->reason));
    if (down->reason == EXIT_REASON_NORMAL) {
        c->stopped_workers++;
    } else if (slot->job_id != -1) {
        c->failed++;
        c->done++;
        slot->job_id = -1;
    }
}

static void DEF(dispatch, Coordinator *c, Pid worker_pid) {
    WorkerSlot *slot = find_worker(c, worker_pid);
    if (!slot) {
        slot = &c->workers[c->known_workers++];
        slot->pid = worker_pid;
        slot->job_id = -1;
        MONITOR(worker_pid);
    }

    if (c->next_job == JOBS) {
        SEND(worker_pid, NULL, 0, MAKE_STR_TAG("worker::stop"));
        return;
    }

    Job job = { c->next_job, 10 + (c->next_job * 3) % (MAX_FIB - 10) };
    c->next_job++;
    slot->job_id = job.id;
    SEND(worker_pid, &job, sizeof job, MAKE_STR_TAG("job"));
}

static void DEF(run_jobs, Coordinator *c) {
    // Every job ends as a result or a crash, then every worker is stopped. A crashed
    // worker is replaced by its restart, so WORKERS workers always end up receiving "stop".
    while (c->done < JOBS || c->stopped_workers < WORKERS) {
        Message *msg = RECEIVE(NO_TAG);

        if (MSG_IS(msg, MAKE_STR_TAG("worker::ready"))) {
            CALL(dispatch, c, msg->sender_pid);
        } else if (MSG_IS(msg, MAKE_STR_TAG("worker::result"))) {
            JobResult *result = msg->payload;
            WorkerSlot *slot = find_worker(c, msg->sender_pid);
            if (slot)
                slot->job_id = -1;
            c->sum += result->result;
            c->done++;
        } else if (msg->tag.kind == TAG_KIND_DOWN) {
            CALL(on_worker_down, c, msg->payload);
        }

        MSG_FREE(msg);
    }
}

// Selective receive: replies are consumed in the reverse order of arrival
static void DEF(echo_demo, Pid echo_pid) {
    for (int i = 1; i <= ECHO_REQUESTS; ++i)
        SEND(echo_pid, &i, sizeof i, MAKE_INT_TAG(i));

    for (int i = ECHO_REQUESTS; i >= 1; --i) {
        Message *reply = RECEIVE(MAKE_INT_TAG(i));
        PRINT("[coordinator] echo reply %d picked out of order\n", *(int *)reply->payload);
        MSG_FREE(reply);
    }
}

TASK(entry, int argc, char *argv[]) {
    if (argc > 1 && strcmp(argv[1], "-i") == 0) {
        PRINT("What is your name? ");
        char *name = GETLINE();
        if (name) {
            PRINT("Hello %s", name);
            FREE(name);
        }
    }

    // Name registry: the stats server is reached through its registered name
    Pid stats_pid = SPAWN0_PRIO_NAMED(stats, "stats_server", PROCESS_PRIORITY_HIGH);
    REGISTER("stats", stats_pid);

    Pid ticker_pid = SPAWN0_PRIO(ticker, PROCESS_PRIORITY_LOW);
    WAIT_NOTIF("ticker_up");

    Pid echo_pid = SPAWN0_NAMED(echo, "echo");

    PRINT("[coordinator] %zu children:", NB_CHILDREN());
    for (size_t i = 0; i < NB_CHILDREN(); ++i)
        PRINT(" %ld", CHILD(i));
    PRINT("\n");

    CALL(echo_demo, echo_pid);

    // Supervision tree
    Coordinator c = {0};

    Pid pool = SPAWN_SUPERVISOR("pool", SUP_ONE_FOR_ONE, POOL_PERIOD_MS, POOL_MAX_RESTARTS);
    for (int i = 0; i < WORKERS; ++i)
        SUP_ADD_CHILD(pool, RES_TRANSIENT, worker, "worker", 2, self->pid, i);

    SUP_ADD_CHILD(pool, RES_TEMPORARY, janitor, "janitor", 1, self->pid);
    WAIT_NOTIF("janitor_ran");

    Pid spare_pid = SUP_ADD_CHILD0(pool, RES_PERMANENT, spare, "spare");
    if (SUP_REMOVE_CHILD(pool, spare_pid) == 0)
        SEND(spare_pid, NULL, 0, MAKE_STR_TAG("spare::stop"));

    CALL(run_jobs, &c);

    // Results
    Message *total = REQUEST(stats_pid, NULL, 0, MAKE_STR_TAG("stats::get"));
    char *summary = FORMAT("[coordinator] %d jobs: %d ok, %d lost to crashes, sum %ld (stats says %ld)\n",
                           JOBS, JOBS - c.failed, c.failed, c.sum, total ? *(long *)total->payload : -1L);
    PRINT("%s", summary);
    FREE(summary);
    MSG_FREE(total);

    int stop = 1;
    SEND(ticker_pid, &stop, sizeof stop, MAKE_INTERNAL_TAG(TICKER_STOP));
    Message *ticks = RECEIVE(MAKE_STR_TAG("ticker::ticks"));
    PRINT("[coordinator] low priority ticker ran %ld times meanwhile\n", *(long *)ticks->payload);
    MSG_FREE(ticks);

    UNREGISTER("stats");
    SEND(stats_pid, NULL, 0, MAKE_STR_TAG("stats::stop"));
    PRINT("[coordinator] after UNREGISTER, WHEREIS(\"stats\") = %ld\n", WHEREIS("stats"));

    // Monitoring a process that is gone (echo answered its 3 requests and exited):
    // the DOWN arrives right away with the "noproc" reason
    MONITOR(echo_pid);
    Message *down = RECEIVE(MAKE_DOWN_TAG(echo_pid));
    PRINT("[coordinator] process %ld is down (%s)\n", echo_pid, reason_name(((DownInfo *)down->payload)->reason));
    MSG_FREE(down);

    EXIT_NORMAL(); // the runtime stops once the entry task ends
}

CLPF_ENTRY(entry)
