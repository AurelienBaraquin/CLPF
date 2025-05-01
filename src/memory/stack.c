#define _GNU_SOURCE
#include <sys/mman.h>
#include <unistd.h>
#include <stdlib.h>
#include <stdio.h>

#define STACK_PAGE_SIZE sysconf(_SC_PAGESIZE)
#define STACK_TOTAL_SIZE (STACK_SIZE + STACK_PAGE_SIZE)

void *stack_alloc(size_t stack_size) {
    // On demande une page supplémentaire pour la guard
    size_t pagesize = STACK_PAGE_SIZE;

    // mmap anonyme, non accessible par défaut
    void *raw = mmap(NULL, stack_size + pagesize, PROT_READ | PROT_WRITE,
                     MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if (raw == MAP_FAILED)
        return NULL;

    // Protéger la première page (guard)
    if (mprotect(raw, pagesize, PROT_NONE) == -1) {
        munmap(raw, stack_size + pagesize);
        return NULL;
    }

    // Retourne la stack utilisable (après la guard page)
    return (char *)raw + pagesize;
}

void stack_free(void *stack_ptr, size_t stack_size) {
    size_t pagesize = STACK_PAGE_SIZE;
    void *raw = (char *)stack_ptr - pagesize;
    munmap(raw, stack_size + pagesize);
}
