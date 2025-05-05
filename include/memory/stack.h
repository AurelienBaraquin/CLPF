#pragma once

#include <stddef.h>

#define STACK_SIZE 64 * 1024 // 16 Ko

void *stack_alloc(size_t stack_size);
void stack_free(void *stack_ptr, size_t stack_size);
