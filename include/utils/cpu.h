#pragma once

#include <sched.h>

// Returns the number of processors this process may run on (honours taskset
// and container CPU limits), or -1 on error.
static inline int nb_cpu(void) {
    cpu_set_t set;
    if (sched_getaffinity(0, sizeof(set), &set) != 0)
        return -1;
    return CPU_COUNT(&set);
}
