#pragma once

// Stack of a process when SpawnOptions.stack_size is 0 (override with -DCLPF_DEFAULT_STACK_SIZE=...)
#ifndef CLPF_DEFAULT_STACK_SIZE
#define CLPF_DEFAULT_STACK_SIZE ((size_t)16 * 1024)
#endif

#include <stddef.h>
#include <libaco/aco.h>

// Process stacks, with a guard page below them. Stacks of CLPF_DEFAULT_STACK_SIZE
// go back to a bounded pool when released; the other sizes are mapped and unmapped.

// A stack ready for a new coroutine, of at least size bytes
aco_share_stack_t *stack_acquire(size_t size);
// Takes back a stack acquired with the same size; no coroutine may use it any more
void stack_release(aco_share_stack_t *stack, size_t size);
// Unmaps the pooled stacks
void stack_pool_drain(void);
// Maps count stacks of the default size into the pool ahead of time, as far as it
// has room; returns how many it holds then
size_t stack_pool_prewarm(size_t count);
size_t stack_pool_count(void);
