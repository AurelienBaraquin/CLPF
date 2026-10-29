#include <stdin_reader.h>
#include <clpf_internal.h>
#include <utils/idle.h>
#include <stdio.h>
#include <stdlib.h>

typedef struct {
    MPSCQNode node;
    Pid requester;
} StdinRequest;

// Never destroyed: the thread may still be blocked in getline when the program ends
static MPSCQ requests;
static IdleWaiter idle;
static pthread_once_t started = PTHREAD_ONCE_INIT;

static bool has_requests(void *arg) {
    (void)arg;
    return !mpscq_empty(&requests);
}

static void *reader_thread(void *arg) {
    (void)arg;
    char *buffer = NULL;
    size_t capacity = 0;

    while (1) {
        idle_wait(&idle, has_requests, NULL);

        MPSCQNode *node = mpscq_drain(&requests);
        while (node) {
            StdinRequest *request = container_of(node, StdinRequest, node);
            node = node->next;

            ssize_t length = getline(&buffer, &capacity, stdin);
            if (length < 0)
                clpf_deliver(INVALID_PID, request->requester, NULL, 0, MAKE_STR_TAG("stdin::eof"));
            else
                clpf_deliver(INVALID_PID, request->requester, buffer, (size_t)length, MAKE_STR_TAG("stdin::line"));
            free(request);
        }
    }
    return NULL;
}

static void start_reader(void) {
    mpscq_init(&requests);
    idle_init(&idle);

    pthread_t thread;
    if (pthread_create(&thread, NULL, reader_thread, NULL) == 0)
        pthread_detach(thread);
}

int stdin_request_line(Pid requester) {
    pthread_once(&started, start_reader);

    StdinRequest *request = malloc(sizeof(StdinRequest));
    if (!request)
        return -1;
    request->requester = requester;
    mpscq_push(&requests, &request->node);
    idle_wake(&idle);
    return 0;
}
