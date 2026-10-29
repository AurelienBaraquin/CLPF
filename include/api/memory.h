#pragma once

#include <clpf_internal.h>

#define ALLOC(size) safe_heap_alloc(&self->heap, size)

#define FREE(ptr) safe_heap_free(&self->heap, ptr)
