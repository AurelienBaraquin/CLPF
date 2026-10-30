#include "test_helpers.h"
#include <api/task.h>
#include <api/message.h>
#include <api/memory.h>
#include <api/timer.h>
#include <api/link.h>
#include <api/table.h>
#include <stdio.h>
#include <string.h>

#define WRITERS         4
#define READERS         4
#define KEYS_PER_WRITER 500
#define MANY_KEYS       10000

TASK(writer, Pid boss, int first) {
    TableId table = TABLE_FIND("shared");
    for (int i = first; i < first + KEYS_PER_WRITER; ++i) {
        char key[32];
        snprintf(key, sizeof key, "key:%d", i);
        TEST_ASSERT(TABLE_PUT(table, key, &i, sizeof i) == 0);
        REDUCE(10);
    }
    SEND(boss, NULL, 0, MAKE_STR_TAG("written"));
}

// Reads while the writers write: a value, when there is one, is always whole
TASK(reader, Pid boss) {
    TableId table = TABLE_FIND("shared");
    for (int round = 0; round < 2000; ++round) {
        int wanted = round % (WRITERS * KEYS_PER_WRITER);
        char key[32];
        snprintf(key, sizeof key, "key:%d", wanted);
        size_t size = 0;
        int *value = TABLE_GET(table, key, &size);
        if (value) {
            TEST_ASSERT(size == sizeof(int) && *value == wanted);
            FREE(value);
        }
        REDUCE(10);
    }
    SEND(boss, NULL, 0, MAKE_STR_TAG("read"));
}

TASK(owner, Pid boss) {
    TableId table = TABLE_NEW("temporary");
    TEST_ASSERT(table != 0 && TABLE_PUT(table, "k", "v", 2) == 0);
    SEND(boss, &table, sizeof table, MAKE_STR_TAG("created"));
    MSG_FREE(RECEIVE(MAKE_STR_TAG("never")));
}

TASK(entry, int argc, char *argv[]) {
    (void)argc;
    (void)argv;

    // Names are unique; ids find tables
    TableId table = TABLE_NEW("config");
    TEST_ASSERT(table != 0);
    TEST_ASSERT(TABLE_NEW("config") == 0 && TABLE_NEW("") == 0);
    TEST_ASSERT(TABLE_FIND("config") == table && TABLE_FIND("missing") == 0);

    // Values are copied in and out
    int timeout = 250;
    TEST_ASSERT(TABLE_PUT(table, "timeout", &timeout, sizeof timeout) == 0);
    timeout = 0;
    size_t size = 0;
    int *stored = TABLE_GET(table, "timeout", &size);
    TEST_ASSERT(stored && size == sizeof(int) && *stored == 250);
    FREE(stored);
    TEST_ASSERT(TABLE_PUT(table, "timeout", "long", 5) == 0);
    char *text = TABLE_GET(table, "timeout", NULL);
    TEST_ASSERT(text && strcmp(text, "long") == 0);
    FREE(text);
    TEST_ASSERT(TABLE_PUT(table, "empty", NULL, 0) == 0);
    TEST_ASSERT(TABLE_GET(table, "empty", &size) != NULL && size == 0);
    TEST_ASSERT(TABLE_COUNT(table) == 2);
    TEST_ASSERT(TABLE_DELETE(table, "empty") == 0 && TABLE_DELETE(table, "empty") == -1);
    TEST_ASSERT(TABLE_GET(table, "empty", &size) == NULL);
    TEST_ASSERT(TABLE_COUNT(table) == 1);

    // It grows
    for (int i = 0; i < MANY_KEYS; ++i) {
        char key[32];
        snprintf(key, sizeof key, "n%d", i);
        TEST_ASSERT(TABLE_PUT(table, key, &i, sizeof i) == 0);
    }
    TEST_ASSERT(TABLE_COUNT(table) == MANY_KEYS + 1);
    int *last = TABLE_GET(table, "n9999", NULL);
    TEST_ASSERT(last && *last == MANY_KEYS - 1);
    FREE(last);

    // Concurrent writers and readers
    TableId shared = TABLE_NEW("shared");
    for (int i = 0; i < WRITERS; ++i)
        SPAWN(writer, 2, self->pid, i * KEYS_PER_WRITER);
    for (int i = 0; i < READERS; ++i)
        SPAWN(reader, 1, self->pid);
    for (int i = 0; i < WRITERS; ++i)
        MSG_FREE(RECEIVE(MAKE_STR_TAG("written")));
    for (int i = 0; i < READERS; ++i)
        MSG_FREE(RECEIVE(MAKE_STR_TAG("read")));
    TEST_ASSERT(TABLE_COUNT(shared) == (long)WRITERS * KEYS_PER_WRITER);

    // A table ends with its owner; only the owner drops it
    Pid pid = SPAWN(owner, 1, self->pid);
    Message *created = RECEIVE(MAKE_STR_TAG("created"));
    TableId temporary = *(TableId *)created->payload;
    MSG_FREE(created);
    TEST_ASSERT(TABLE_FIND("temporary") == temporary && TABLE_COUNT(temporary) == 1);
    TEST_ASSERT(TABLE_DROP(temporary) == -1);
    KILL(pid);
    for (int i = 0; i < 100 && TABLE_FIND("temporary") != 0; ++i)
        SLEEP(5);
    TEST_ASSERT(TABLE_FIND("temporary") == 0);
    TEST_ASSERT(TABLE_COUNT(temporary) == -1 && TABLE_GET(temporary, "k", NULL) == NULL);
    TEST_ASSERT(TABLE_PUT(temporary, "k", "v", 2) == -1);

    // A dropped id stays dead, even when its name is reused
    TEST_ASSERT(TABLE_DROP(table) == 0);
    TableId again = TABLE_NEW("config");
    TEST_ASSERT(again != 0 && again != table && TABLE_COUNT(table) == -1 && TABLE_COUNT(again) == 0);

    TEST_PASS();
}

CLPF_TEST_MAIN(entry)
