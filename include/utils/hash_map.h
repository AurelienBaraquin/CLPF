#pragma once

#include <stddef.h>

// A map from strings to pointers, with chained buckets that double once
// there are as many keys. Not thread-safe: callers bring their own lock.
typedef struct HashMap HashMap;

// NULL if out of memory
HashMap *hash_map_new(void);
// free_value, if not NULL, is called with each value
void hash_map_free(HashMap *map, void (*free_value)(void *value));

// The value stored under key, or NULL
void *hash_map_get(const HashMap *map, const char *key);
// The map's own copy of key, valid until key is removed, or NULL
const char *hash_map_find_key(const HashMap *map, const char *key);
// Stores value under a copy of key, replacing any value; returns that copy,
// or NULL if out of memory (the map is then unchanged)
const char *hash_map_put(HashMap *map, const char *key, void *value);
// Removes key; returns its value, or NULL if it was not there
void *hash_map_remove(HashMap *map, const char *key);
size_t hash_map_count(const HashMap *map);
