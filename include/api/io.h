#pragma once

#include <clpf_internal.h>

#define PRINT(fmt, ...) \
    __print(self, fmt, ##__VA_ARGS__)

#define FORMAT(fmt, ...) \
    __format(self, fmt, ##__VA_ARGS__)

#define GETLINE() \
    __getline(self)

char *__vformat(Process *self, const char *fmt, va_list ap);
char *__format(Process *self, const char *fmt, ...);
int __print(Process *self, const char *fmt, ...);

char* __getline(Process *self);
