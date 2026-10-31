#pragma once

#include <clpf_internal.h>

// Allocates in the process heap. NULL if out of memory; past the process's
// max_heap_size, the process ends with EXIT_REASON_HEAP_LIMIT instead.
#define ALLOC(size) clpf_alloc(self, size)

#define FREE(ptr) safe_heap_free(&self->heap, ptr)
