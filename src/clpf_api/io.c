#include <stb_sprintf/stb_sprintf.h>
#include <api/io.h>
#include <api/message.h>
#include <api/registry.h>
#include <stdin_reader.h>

char *clpf_vformat(Process *self, const char *fmt, va_list ap) {
    char tmp[128];
    va_list ap_copy;
    va_copy(ap_copy, ap);

    int len = stbsp_vsnprintf(tmp, sizeof(tmp), fmt, ap_copy);
    va_end(ap_copy);

    if (len < 0)
        return NULL;

    char *out;
    if (len < (int)sizeof(tmp)) {
        out = safe_heap_alloc(&self->heap, len + 1);
        if (!out)
            return NULL;
        memcpy(out, tmp, len + 1);
    } else {
        out = safe_heap_alloc(&self->heap, len + 1);
        if (!out)
            return NULL;
        stbsp_vsnprintf(out, len + 1, fmt, ap);
    }

    return out;
}

char *clpf_format(Process *self, const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    char *result = clpf_vformat(self, fmt, ap);
    va_end(ap);
    return result;
}

int clpf_print(Process *self, const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    char *str = clpf_vformat(self, fmt, ap);
    va_end(ap);

    if (!str)
        return -1;

    Pid io = WHEREIS("server::io");
    if (io != -1) {
        SEND(io, str, strlen(str), MAKE_STR_TAG("server::io::print"));
    }

    safe_heap_free(&self->heap, str);
    return 0;
}

char *clpf_getline(Process *self) {
    if (stdin_request_line(self->pid) != 0)
        return NULL;

    Message *reply = RECEIVE_IF(TM_PREFIX, MAKE_STR_TAG("stdin::"));
    if (MSG_IS(reply, MAKE_STR_TAG("stdin::eof"))) {
        MSG_FREE(reply);
        return NULL;
    }

    char *line = ALLOC(reply->size + 1);
    if (line) {
        memcpy(line, reply->payload, reply->size);
        line[reply->size] = '\0';
    }
    MSG_FREE(reply);
    return line;
}
