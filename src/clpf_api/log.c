#include <api/log.h>
#include <api/io.h>
#include <api/message.h>
#include <api/registry.h>
#include <stb_sprintf/stb_sprintf.h>
#include <stdlib.h>
#include <strings.h>
#include <time.h>
#include <unistd.h>

static const char *const LEVEL_NAMES[] = {"debug", "info", "warn", "error", "none"};
static const char *const LEVEL_LABELS[] = {"DEBUG", "INFO ", "WARN ", "ERROR"};

static _Atomic LogLevel current_level = LOG_LEVEL_INFO;

void clpf_log_init(void) {
    const char *value = getenv("CLPF_LOG_LEVEL");
    if (!value)
        return;
    for (LogLevel level = LOG_LEVEL_DEBUG; level <= LOG_LEVEL_NONE; ++level) {
        if (strcasecmp(value, LEVEL_NAMES[level]) == 0) {
            clpf_set_log_level(level);
            return;
        }
    }
    fprintf(stderr, "CLPF_LOG_LEVEL: unknown level \"%s\", using info\n", value);
}

void clpf_set_log_level(LogLevel level) {
    atomic_store(&current_level, level);
}

LogLevel clpf_log_level(void) {
    return atomic_load(&current_level);
}

int clpf_log(Process *self, LogLevel level, const char *fmt, ...) {
    if (level < clpf_log_level() || level >= LOG_LEVEL_NONE)
        return 0;

    va_list ap;
    va_start(ap, fmt);
    char *text = clpf_vformat(self, fmt, ap);
    va_end(ap);
    if (!text)
        return -1;

    struct timespec now;
    struct tm local;
    clock_gettime(CLOCK_REALTIME, &now);
    localtime_r(&now.tv_sec, &local);
    char *line = FORMAT("%02d:%02d:%02d.%03ld %s %s<%ld>: %s\n", local.tm_hour, local.tm_min, local.tm_sec,
                        now.tv_nsec / 1000000, LEVEL_LABELS[level], self->name[0] ? self->name : "-", self->pid, text);
    safe_heap_free(&self->heap, text);
    if (!line)
        return -1;

    // Without the I/O server, at shutdown, a log line still matters more than its order
    Pid io = WHEREIS("server::io");
    size_t length = strlen(line);
    if (io == INVALID_PID || SEND(io, line, length, MAKE_ATOM_TAG("server::io::log")) != 0)
        (void)!write(STDERR_FILENO, line, length);
    safe_heap_free(&self->heap, line);
    return 0;
}
