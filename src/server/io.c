#include <server/io.h>
#include <api/message.h>

#include <stb_sprintf/stb_sprintf.h>

typedef enum __server_io__e {
    S_IO_NOTHING,
    S_IO_STOP,
    S_IO_SUCCESS,
} S_IO;

static S_IO server_io_compute_message(Message *msg) {
    if (!msg)
        return S_IO_NOTHING;

    if (MessageMatchFn_SameTag(msg, MAKE_STR_TAG("server::io::print"))) {
        write(STDOUT_FILENO, msg->payload, msg->size);
        return S_IO_SUCCESS;
    }

    if (MessageMatchFn_SameTag(msg, MAKE_STR_TAG("core::event::stop"))) {
        return S_IO_STOP;
    }
}

TASK0(__server_io__) {
    bool running = true;
    Message *msg = NULL;

    while (running) {
        msg = __receive_internal(self, NO_TAG);

        S_IO r = server_io_compute_message(msg);

        switch (r) {
            case S_IO_NOTHING:
                break;
            case S_IO_STOP:
                running = false;
                break;
            case S_IO_SUCCESS:
                if (msg->payload)
                    safe_heap_free((safe_heap_t *)msg->heap, msg->payload);
                safe_heap_free((safe_heap_t *)msg->heap, msg);
                break;

            default:
                break;
        }
    }
    EXIT();
}
