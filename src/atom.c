#include <mailbox.h>
#include <utils/hash_map.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>

// Atoms live as long as the program: the map is never freed
static pthread_mutex_t lock = PTHREAD_MUTEX_INITIALIZER;
static HashMap *atoms; // Guarded by lock; the keys are the atoms

const char *clpf_atom(const char *name) {
    pthread_mutex_lock(&lock);
    if (!atoms)
        atoms = hash_map_new();
    const char *atom = atoms ? hash_map_find_key(atoms, name) : NULL;
    if (atoms && !atom)
        atom = hash_map_put(atoms, name, NULL);
    pthread_mutex_unlock(&lock);

    if (!atom) {
        fprintf(stderr, "clpf: out of memory for the atom \"%s\"\n", name);
        abort();
    }
    return atom;
}

Tag clpf_atom_tag(const char *name) {
    return (Tag){.kind = TAG_KIND_ATOM, .str_value = clpf_atom(name)};
}
