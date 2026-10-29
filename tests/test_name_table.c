#include <utils/name_table.h>
#include <stdio.h>

static int failures = 0;

#define CHECK(cond)                                                           \
    do {                                                                      \
        if (!(cond)) {                                                        \
            fprintf(stderr, "%s:%d: check failed: %s\n",                      \
                    __FILE__, __LINE__, #cond);                               \
            ++failures;                                                       \
        }                                                                     \
    } while (0)

int main(void) {
    TrieNode *table = name_table_init();
    CHECK(table != NULL);

    // ':' must not share a slot with any other character
    CHECK(name_table_insert(table, "server::io", 1) == 0);
    CHECK(name_table_lookup(table, "server66io") == INVALID_PID);
    CHECK(name_table_insert(table, "a66b", 2) == 0);
    CHECK(name_table_lookup(table, "a::b") == INVALID_PID);
    CHECK(name_table_lookup(table, "a66b") == 2);
    CHECK(name_table_lookup(table, "server::io") == 1);

    // A prefix of a registered name is not itself registered
    CHECK(name_table_lookup(table, "server") == INVALID_PID);

    name_table_remove(table, "server::io");
    CHECK(name_table_lookup(table, "server::io") == INVALID_PID);
    CHECK(name_table_lookup(table, "a66b") == 2);

    // Characters outside the alphabet and empty names are rejected
    CHECK(name_table_insert(table, "bad name", 3) == -1);
    CHECK(name_table_insert(table, "", 4) == -1);
    CHECK(name_table_lookup(table, "") == INVALID_PID);
    CHECK(name_table_lookup(table, "unknown") == INVALID_PID);

    name_table_free(table);
    return failures == 0 ? 0 : 1;
}
