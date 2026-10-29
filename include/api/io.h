#pragma once

#include <clpf_internal.h>

#define PRINT(fmt, ...) \
    clpf_print(self, fmt, ##__VA_ARGS__)

#define FORMAT(fmt, ...) \
    clpf_format(self, fmt, ##__VA_ARGS__)

#define GETLINE() \
    clpf_getline(self)

char *clpf_vformat(Process *self, const char *fmt, va_list ap);
char *clpf_format(Process *self, const char *fmt, ...);
int clpf_print(Process *self, const char *fmt, ...);

char* clpf_getline(Process *self);
