#pragma once

#include <utils/hash_map.h>
#include <utils/pid.h>

// Registered names: a-z, A-Z, 0-9, '_' and ':', mapped to pids. Not
// thread-safe: the runtime guards it with its table lock.
typedef HashMap NameTable;

// NULL if out of memory
NameTable *name_table_init(void);
void name_table_free(NameTable *table);

// Maps name to pid, replacing any pid; returns -1 if the name is empty, has
// an invalid character, or out of memory
int name_table_insert(NameTable *table, const char *name, Pid pid);
// The pid of a name, or INVALID_PID
Pid name_table_lookup(NameTable *table, const char *name);
void name_table_remove(NameTable *table, const char *name);
