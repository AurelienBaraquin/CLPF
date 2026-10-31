// A TCP chat server built on CLPF processes.
//
//   example_chat serve [port]                      runs the server (default port 7000)
//   example_chat selftest [users rooms messages]   runs it against simulated users
//
// Try it with two terminals: example_chat serve, then nc localhost 7000 and
// type NICK alice, JOIN lobby, MSG lobby hello, WHO lobby, PRIV bob hi, QUIT.
#include "chat.h"
#include <api/message.h>
#include <api/io.h>
#include <api/log.h>
#include <stdlib.h>
#include <string.h>

#define DEFAULT_PORT     7000
#define SERVE_IDLE_MS    (10 * 60 * 1000)
#define SELFTEST_IDLE_MS 3000

static int argument(int argc, char *argv[], int index, int fallback) {
    if (argc <= index)
        return fallback;
    char *end = NULL;
    long value = strtol(argv[index], &end, 10);
    return end != argv[index] && *end == '\0' && value > 0 && value < 1000000 ? (int)value : fallback;
}

TASK(entry, int argc, char *argv[]) {
    if (argc > 1 && strcmp(argv[1], "serve") == 0) {
        int port = chat_start(self, argument(argc, argv, 2, DEFAULT_PORT), false, SERVE_IDLE_MS);
        if (port < 0) {
            PRINT("cannot listen on port %d\n", argument(argc, argv, 2, DEFAULT_PORT));
            EXIT_ERROR();
        }
        PRINT("chat server on port %d: try nc localhost %d\n", port, port);
        MSG_FREE(RECEIVE(MAKE_ATOM_TAG("never"))); // Serves until killed
    }

    if (argc > 1 && strcmp(argv[1], "selftest") != 0) {
        PRINT("usage: %s [serve [port] | selftest [users rooms messages]]\n", argv[0]);
        EXIT_ERROR();
    }
    // The crash and the cut-off client are expected: keep their warnings quiet
    clpf_set_log_level(LOG_LEVEL_ERROR);
    int port = chat_start(self, 0, true, SELFTEST_IDLE_MS);
    if (port < 0 || !chat_selftest(self, port, argument(argc, argv, 2, 100), argument(argc, argv, 3, 10),
                                   argument(argc, argv, 4, 20), SELFTEST_IDLE_MS))
        EXIT_ERROR();
}

CLPF_ENTRY(entry)
