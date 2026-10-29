#pragma once

// Stack of a process when SpawnOptions.stack_size is 0 (override with -DCLPF_DEFAULT_STACK_SIZE=...)
#ifndef CLPF_DEFAULT_STACK_SIZE
#define CLPF_DEFAULT_STACK_SIZE ((size_t)16 * 1024)
#endif
