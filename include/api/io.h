#pragma once

#include <clpf_internal.h>

#define PRINT(fmt, ...) \
    clpf_print(self, fmt, ##__VA_ARGS__)

#define FORMAT(fmt, ...) \
    clpf_format(self, fmt, ##__VA_ARGS__)

// Waits for a line of stdin without blocking anything else; returns it with its
// newline, allocated in the caller's heap (FREE it), or NULL at the end of input
#define GETLINE() \
    clpf_getline(self)

char *clpf_vformat(Process *self, const char *fmt, va_list ap);
char *clpf_format(Process *self, const char *fmt, ...);
int clpf_print(Process *self, const char *fmt, ...);

char* clpf_getline(Process *self);
