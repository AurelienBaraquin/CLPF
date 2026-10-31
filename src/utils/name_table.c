#include <utils/name_table.h>
#include <stdbool.h>
#include <stdlib.h>

static bool valid_name(const char *name) {
    if (*name == '\0')
        return false;
    for (const char *c = name; *c; ++c) {
        bool letter = (*c >= 'a' && *c <= 'z') || (*c >= 'A' && *c <= 'Z');
        bool digit = *c >= '0' && *c <= '9';
        if (!letter && !digit && *c != '_' && *c != ':')
            return false;
    }
    return true;
}

NameTable *name_table_init(void) {
    return hash_map_new();
}

void name_table_free(NameTable *table) {
    hash_map_free(table, free);
}

int name_table_insert(NameTable *table, const char *name, Pid pid) {
    if (!valid_name(name))
        return -1;

    Pid *stored = hash_map_get(table, name);
    if (stored) {
        *stored = pid;
        return 0;
    }
    stored = malloc(sizeof(Pid));
    if (!stored)
        return -1;
    *stored = pid;
    if (!hash_map_put(table, name, stored)) {
        free(stored);
        return -1;
    }
    return 0;
}

Pid name_table_lookup(NameTable *table, const char *name) {
    Pid *stored = hash_map_get(table, name);
    return stored ? *stored : INVALID_PID;
}

void name_table_remove(NameTable *table, const char *name) {
    free(hash_map_remove(table, name));
}
