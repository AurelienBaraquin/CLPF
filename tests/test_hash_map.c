#include <utils/hash_map.h>
#include <stdio.h>
#include <string.h>

#define MANY 10000

static int failures = 0;

#define CHECK(cond)                                                                  \
    do {                                                                             \
        if (!(cond)) {                                                               \
            fprintf(stderr, "%s:%d: check failed: %s\n", __FILE__, __LINE__, #cond); \
            ++failures;                                                              \
        }                                                                            \
    } while (0)

static int freed = 0;

static void count_free(void *value) {
    (void)value;
    ++freed;
}

static void sum_values(const char *key, void *value, void *arg) {
    (void)key;
    *(long *)arg += *(int *)value;
}

int main(void) {
    HashMap *map = hash_map_new();
    CHECK(map != NULL);
    if (!map)
        return 1;

    int one = 1, two = 2;
    CHECK(hash_map_get(map, "one") == NULL && hash_map_count(map) == 0);
    CHECK(hash_map_put(map, "one", &one) != NULL);
    CHECK(hash_map_get(map, "one") == &one);

    // The map keeps its own copy of the key, found again by any equal string
    char key[8];
    strcpy(key, "one");
    const char *stored = hash_map_find_key(map, key);
    CHECK(stored != NULL && stored != key && strcmp(stored, "one") == 0);
    CHECK(hash_map_put(map, key, &two) == stored); // replacing keeps the key
    CHECK(hash_map_get(map, "one") == &two && hash_map_count(map) == 1);

    // Growing keeps every entry
    static int values[MANY];
    for (int i = 0; i < MANY; ++i) {
        char name[32];
        snprintf(name, sizeof name, "key:%d", i);
        values[i] = i;
        CHECK(hash_map_put(map, name, &values[i]) != NULL);
    }
    CHECK(hash_map_count(map) == MANY + 1);
    for (int i = 0; i < MANY; i += 97) {
        char name[32];
        snprintf(name, sizeof name, "key:%d", i);
        int *value = hash_map_get(map, name);
        CHECK(value && *value == i);
    }

    // Visiting sees every entry once
    long visited_sum = 0;
    hash_map_for_each(map, sum_values, &visited_sum);
    CHECK(visited_sum == (long)MANY * (MANY - 1) / 2 + 2);

    // Removing returns the value; the others stay reachable
    CHECK(hash_map_remove(map, "key:5") == &values[5]);
    CHECK(hash_map_remove(map, "key:5") == NULL && hash_map_get(map, "key:5") == NULL);
    CHECK(hash_map_get(map, "key:6") == &values[6] && hash_map_count(map) == MANY);

    hash_map_free(map, count_free);
    CHECK(freed == MANY);

    if (failures == 0)
        printf("hash_map: all checks passed\n");
    return failures == 0 ? 0 : 1;
}
