#pragma once

#include <clpf_internal.h>

// Shared tables: named key-value stores that every process can read and
// write without messages, like Erlang's ETS. Keys are strings, values are
// bytes, copied in by TABLE_PUT and out by TABLE_GET. A table belongs to the
// process that created it, and is deleted when that process ends.
// Readers run in parallel; a writer has the table to itself.

// 0 is never a table
typedef uint64_t TableId;

// A new table named name (at most 63 characters), or 0 if the name is invalid or taken
#define TABLE_NEW(name) clpf_table_new(self, name)
// The table with that name, or 0
#define TABLE_FIND(name) clpf_table_find(name)
// Deletes a table: 0, or -1 if it does not exist or the caller does not own it
#define TABLE_DROP(id) clpf_table_drop(self, id)
// Stores a copy of size bytes under key, replacing any value: 0, or -1 if
// the table does not exist or out of memory
#define TABLE_PUT(id, key, data, size) clpf_table_put(id, key, data, size)
// A copy of the value in the caller's heap (FREE it), its size in *size_out
// if not NULL, or NULL if there is no such table or key, or out of memory
#define TABLE_GET(id, key, size_out) clpf_table_get(self, id, key, size_out)
// 0, or -1 if there is no such table or key
#define TABLE_DELETE(id, key) clpf_table_delete(id, key)
// Number of keys, or -1 if the table does not exist
#define TABLE_COUNT(id) clpf_table_count(id)

TableId clpf_table_new(Process *self, const char *name);
TableId clpf_table_find(const char *name);
int clpf_table_drop(Process *self, TableId id);
int clpf_table_put(TableId id, const char *key, const void *data, size_t size);
void *clpf_table_get(Process *self, TableId id, const char *key, size_t *size_out);
int clpf_table_delete(TableId id, const char *key);
long clpf_table_count(TableId id);
