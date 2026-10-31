#include <api/table.h>
#include <tables.h>
#include <utils/hash_map.h>
#include <stdlib.h>
#include <string.h>

#define FIRST_BUCKETS 16

// One allocation per key: the value, aligned, then the key
typedef struct Entry {
    struct Entry *next;
    uint64_t hash;
    size_t size;
    _Alignas(16) unsigned char data[];
} Entry;

typedef struct {
    char name[NAME_MAX_LENGTH];
    size_t slot_index; // Its slot in the directory
    Pid owner;
    pthread_rwlock_t lock; // Guards everything below
    Entry **buckets;
    size_t bucket_count; // A power of two
    size_t count;
} Table;

// A TableId is (generation << 32) | (index + 1): a deleted table's id never
// designates a later table in the same slot
typedef struct {
    Table *table; // NULL when free
    uint32_t generation;
} Slot;

static pthread_mutex_t directory_lock = PTHREAD_MUTEX_INITIALIZER;
static Slot *slots; // Guarded by directory_lock, as slot_count and names
static size_t slot_count;
static HashMap *names; // name -> the live Table

static uint64_t hash_key(const char *key) {
    uint64_t hash = 14695981039346656037ULL; // FNV-1a
    for (const unsigned char *c = (const unsigned char *)key; *c; ++c)
        hash = (hash ^ *c) * 1099511628211ULL;
    return hash;
}

static const char *entry_key(const Entry *entry) {
    return (const char *)entry->data + entry->size;
}

static bool valid_name(const char *name) {
    return name && name[0] != '\0' && strlen(name) < NAME_MAX_LENGTH;
}

// Index of the live table with that name, or -1
static long find_index(const char *name) {
    const Table *table = names ? hash_map_get(names, name) : NULL;
    return table ? (long)table->slot_index : -1;
}

static Slot *slot_of(TableId id) {
    size_t index = (size_t)(id & UINT32_MAX);
    if (index == 0 || index > slot_count)
        return NULL;
    Slot *slot = &slots[index - 1];
    return slot->table && slot->generation == (uint32_t)(id >> 32) ? slot : NULL;
}

static TableId id_of(size_t index) {
    return ((uint64_t)slots[index].generation << 32) | (index + 1);
}

// Locks a live table for reading or writing, or returns NULL. The table lock
// is taken before the directory is released, so a deletion waits for it.
static Table *acquire(TableId id, bool write) {
    pthread_mutex_lock(&directory_lock);
    Slot *slot = slot_of(id);
    Table *table = slot ? slot->table : NULL;
    if (table) {
        if (write)
            pthread_rwlock_wrlock(&table->lock);
        else
            pthread_rwlock_rdlock(&table->lock);
    }
    pthread_mutex_unlock(&directory_lock);
    return table;
}

static Entry **find_entry(Table *table, const char *key, uint64_t hash) {
    Entry **link = &table->buckets[hash & (table->bucket_count - 1)];
    while (*link && ((*link)->hash != hash || strcmp(entry_key(*link), key) != 0))
        link = &(*link)->next;
    return link;
}

// Doubles the buckets once there are as many keys; a failure only costs speed
static void grow(Table *table) {
    if (table->count < table->bucket_count)
        return;
    size_t bucket_count = table->bucket_count * 2;
    Entry **buckets = (Entry **)calloc(bucket_count, sizeof(Entry *));
    if (!buckets)
        return;
    for (size_t i = 0; i < table->bucket_count; ++i) {
        for (Entry *entry = table->buckets[i], *next; entry; entry = next) {
            next = entry->next;
            Entry **bucket = &buckets[entry->hash & (bucket_count - 1)];
            entry->next = *bucket;
            *bucket = entry;
        }
    }
    free((void *)table->buckets);
    table->buckets = buckets;
    table->bucket_count = bucket_count;
}

static void destroy(Table *table) {
    for (size_t i = 0; i < table->bucket_count; ++i) {
        for (Entry *entry = table->buckets[i], *next; entry; entry = next) {
            next = entry->next;
            free(entry);
        }
    }
    free((void *)table->buckets);
    pthread_rwlock_destroy(&table->lock);
    free(table);
}

// Takes the table out of the directory (held), then waits for the operations in progress
static Table *detach(Slot *slot) {
    Table *table = slot->table;
    slot->table = NULL;
    ++slot->generation;
    hash_map_remove(names, table->name);
    pthread_rwlock_wrlock(&table->lock);
    pthread_rwlock_unlock(&table->lock);
    return table;
}

static Slot *free_slot(size_t *index) {
    for (size_t i = 0; i < slot_count; ++i) {
        if (!slots[i].table) {
            *index = i;
            return &slots[i];
        }
    }
    size_t count = slot_count ? slot_count * 2 : 8;
    Slot *grown = realloc(slots, count * sizeof(Slot));
    if (!grown)
        return NULL;
    memset(&grown[slot_count], 0, (count - slot_count) * sizeof(Slot));
    slots = grown;
    *index = slot_count;
    slot_count = count;
    return &slots[*index];
}

TableId clpf_table_new(Process *self, const char *name) {
    if (!valid_name(name))
        return 0;

    Table *table = calloc(1, sizeof(Table));
    Entry **buckets = (Entry **)calloc(FIRST_BUCKETS, sizeof(Entry *));
    if (!table || !buckets) {
        free(table);
        free((void *)buckets);
        return 0;
    }
    memcpy(table->name, name, strlen(name) + 1); // length checked by valid_name
    table->owner = self->pid;
    pthread_rwlock_init(&table->lock, NULL);
    table->buckets = buckets;
    table->bucket_count = FIRST_BUCKETS;

    TableId id = 0;
    pthread_mutex_lock(&directory_lock);
    size_t index = 0;
    if (!names)
        names = hash_map_new();
    Slot *slot = !names || find_index(name) >= 0 ? NULL : free_slot(&index);
    if (slot && hash_map_put(names, name, table)) {
        table->slot_index = index;
        slot->table = table;
        id = id_of(index);
        ++self->table_count;
    }
    pthread_mutex_unlock(&directory_lock);

    if (!id)
        destroy(table);
    return id;
}

TableId clpf_table_find(const char *name) {
    if (!valid_name(name))
        return 0;

    pthread_mutex_lock(&directory_lock);
    long index = find_index(name);
    TableId id = index >= 0 ? id_of((size_t)index) : 0;
    pthread_mutex_unlock(&directory_lock);
    return id;
}

int clpf_table_drop(Process *self, TableId id) {
    Table *table = NULL;
    pthread_mutex_lock(&directory_lock);
    Slot *slot = slot_of(id);
    if (slot && slot->table->owner == self->pid) {
        table = detach(slot);
        --self->table_count;
    }
    pthread_mutex_unlock(&directory_lock);

    if (!table)
        return -1;
    destroy(table);
    return 0;
}

void tables_forget(Pid owner) {
    pthread_mutex_lock(&directory_lock);
    for (size_t i = 0; i < slot_count; ++i) {
        if (slots[i].table && slots[i].table->owner == owner)
            destroy(detach(&slots[i]));
    }
    pthread_mutex_unlock(&directory_lock);
}

int clpf_table_put(TableId id, const char *key, const void *data, size_t size) {
    if (!key || (!data && size > 0))
        return -1;

    size_t key_size = strlen(key) + 1;
    Entry *entry = malloc(sizeof(Entry) + size + key_size);
    if (!entry)
        return -1;
    entry->hash = hash_key(key);
    entry->size = size;
    if (size > 0)
        memcpy(entry->data, data, size);
    memcpy(entry->data + size, key, key_size);

    Table *table = acquire(id, true);
    if (!table) {
        free(entry);
        return -1;
    }
    Entry **link = find_entry(table, key, entry->hash);
    Entry *replaced = *link;
    entry->next = replaced ? replaced->next : NULL;
    *link = entry;
    if (!replaced) {
        ++table->count;
        grow(table);
    }
    pthread_rwlock_unlock(&table->lock);

    free(replaced);
    return 0;
}

void *clpf_table_get(Process *self, TableId id, const char *key, size_t *size_out) {
    if (!key)
        return NULL;

    Table *table = acquire(id, false);
    if (!table)
        return NULL;
    Entry *entry = *find_entry(table, key, hash_key(key));
    // One byte at least, so that an empty value is not mistaken for a missing one
    void *copy = entry ? safe_heap_alloc(&self->heap, entry->size ? entry->size : 1) : NULL;
    if (copy) {
        memcpy(copy, entry->data, entry->size);
        if (size_out)
            *size_out = entry->size;
    }
    pthread_rwlock_unlock(&table->lock);
    return copy;
}

int clpf_table_delete(TableId id, const char *key) {
    if (!key)
        return -1;

    Table *table = acquire(id, true);
    if (!table)
        return -1;
    Entry **link = find_entry(table, key, hash_key(key));
    Entry *removed = *link;
    if (removed) {
        *link = removed->next;
        --table->count;
    }
    pthread_rwlock_unlock(&table->lock);

    free(removed);
    return removed ? 0 : -1;
}

long clpf_table_count(TableId id) {
    Table *table = acquire(id, false);
    if (!table)
        return -1;
    long count = (long)table->count;
    pthread_rwlock_unlock(&table->lock);
    return count;
}
