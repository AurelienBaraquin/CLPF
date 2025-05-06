#pragma once

#include <utils/pid.h>

#define ALPHABET_SIZE 63  // For extended ASCII

typedef struct TrieNode {
    struct TrieNode* children[ALPHABET_SIZE];
    Pid pid;
    int is_end;
} TrieNode;

// Initializes the table structure
TrieNode* name_table_init(void);

// Frees all the memory used
void name_table_free(TrieNode *root);

// Inserts a name with its Pid
int name_table_insert(TrieNode *root, const char* name, Pid pid);

// Searches for a Pid associated with a name, returns -1 if not found
Pid name_table_lookup(TrieNode *root, const char* name);

// Removes a name from the table
void name_table_remove(TrieNode *root, const char* name);
