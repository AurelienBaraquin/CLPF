#include <native_task/server/io.h>
#include <api/message.h>
#include <errno.h>
#include <unistd.h>
#include <api/registry.h>
#include <api/link.h>

#include <stb_sprintf/stb_sprintf.h>

typedef enum server_io_e {
    S_IO_NOTHING,
    S_IO_STOP,
    S_IO_SUCCESS,
} S_IO;

// write() may write less than asked (pipes, large outputs): loop until all is out
static void write_all(int fd, const char *data, size_t size) {
    while (size > 0) {
        ssize_t written = write(fd, data, size);
        if (written < 0) {
            if (errno == EINTR)
                continue;
            return;
        }
        data += written;
        size -= (size_t)written;
    }
}

static S_IO server_io_compute_message(Message *msg) {
    if (!msg)
        return S_IO_NOTHING;

    if (MSG_IS(msg, MAKE_STR_TAG("server::io::print"))) {
        write_all(STDOUT_FILENO, msg->payload, msg->size);
        return S_IO_SUCCESS;
    }

    // Asked to stop: everything printed before is already written, mailboxes being FIFO
    if (msg->tag.kind == TAG_KIND_EXIT && ((ExitInfo *)msg->payload)->reason != EXIT_REASON_NORMAL) {
        return S_IO_STOP;
    }

    return S_IO_NOTHING;
}

TASK0(clpf_server_io) {
    bool running = true;
    Message *msg = NULL;

    // PRINT and GETLINE find the server by name: the launcher registers the first
    // server before anything can print, a restarted one takes the name back
    if (WHEREIS("server::io") != self->pid && REGISTER("server::io", self->pid) != 0)
        EXIT_ERROR();
    TRAP_EXIT(true);

    while (running) {
        msg = RECEIVE(NO_TAG);

        S_IO r = server_io_compute_message(msg);

        switch (r) {
            case S_IO_NOTHING:
            case S_IO_SUCCESS: break;
            case S_IO_STOP: running = false; break;

            default: break;
        }

        MSG_FREE(msg);
    }
    EXIT_NORMAL();
}
