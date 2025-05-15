#include <native_task/server/io.h>
#include <api/message.h>

#include <stb_sprintf/stb_sprintf.h>

typedef enum __server_io__e {
    S_IO_NOTHING,
    S_IO_STOP,
    S_IO_SUCCESS,
} S_IO;

static S_IO DEF(server_io_compute_message, Message *msg) {
    if (!msg)
        return S_IO_NOTHING;

    if (MessageMatchFn_SameTag(msg, MAKE_STR_TAG("server::io::print"))) {
        write(STDOUT_FILENO, msg->payload, msg->size);
        return S_IO_SUCCESS;
    }

    if (MessageMatchFn_SameTag(msg, MAKE_STR_TAG("server::io::getline"))) {
        Pid dest = msg->sender_pid;
        if (dest == -1) {
            fprintf(stderr, "[IO SERVER] Invalid PID: %ld\n", dest);
            return S_IO_NOTHING;
        }
        char *buffer = NULL;
        size_t size = 0;
        ssize_t len = getline(&buffer, &size, stdin);
        if (len < 0) {
            perror("getline");
            return S_IO_NOTHING;
        }
        SEND(dest, buffer, len, MAKE_STR_TAG("server::io::getline"));
    }

    if (MessageMatchFn_SameTag(msg, MAKE_STR_TAG("core::event::stop"))) {
        return S_IO_STOP;
    }
}

TASK0(__server_io__) {
    bool running = true;
    Message *msg = NULL;

    while (running) {
        msg = RECEIVE_RAW(NO_TAG);

        S_IO r = CALL(server_io_compute_message, msg);

        switch (r) {
            case S_IO_NOTHING:
            case S_IO_SUCCESS:
                break;
            case S_IO_STOP:
                running = false;
                break;

            default:
                break;
        }

        if (msg->payload)
            FREE(msg->payload);
        FREE(msg);
    }
    EXIT_NORMAL();
}
