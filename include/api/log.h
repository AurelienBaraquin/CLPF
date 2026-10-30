#pragma once

#include <clpf_internal.h>

typedef enum {
    LOG_LEVEL_DEBUG,
    LOG_LEVEL_INFO,
    LOG_LEVEL_WARN,
    LOG_LEVEL_ERROR,
    LOG_LEVEL_NONE, // Logs nothing
} LogLevel;

// Writes one line to stderr through the I/O server, so that lines never mix:
// "12:34:56.789 WARN  name<pid>: message". Lines below the current level are
// dropped. Returns 0, or -1 if out of memory. No newline needed in fmt.
#define LOG_DEBUG(fmt, ...) clpf_log(self, LOG_LEVEL_DEBUG, fmt, ##__VA_ARGS__)
#define LOG_INFO(fmt, ...)  clpf_log(self, LOG_LEVEL_INFO, fmt, ##__VA_ARGS__)
#define LOG_WARN(fmt, ...)  clpf_log(self, LOG_LEVEL_WARN, fmt, ##__VA_ARGS__)
#define LOG_ERROR(fmt, ...) clpf_log(self, LOG_LEVEL_ERROR, fmt, ##__VA_ARGS__)

int clpf_log(Process *self, LogLevel level, const char *fmt, ...);

// The level starts from the CLPF_LOG_LEVEL environment variable (debug, info,
// warn, error or none), info by default. It applies to all processes.
void clpf_set_log_level(LogLevel level);
LogLevel clpf_log_level(void);
