#include <unistd.h>

// This function returns the number of online processors in the system.
// Returns -1 on error.
static inline int nb_cpu(void) {
    return sysconf(_SC_NPROCESSORS_ONLN);
}
