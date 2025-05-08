#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <utils/name_table.h>

#define ALPHABET_SIZE 63

static TrieNode* create_node() {
    TrieNode* node = (TrieNode*)calloc(1, sizeof(TrieNode));
    node->pid = -1;
    return node;
}

// a-z => 0-25, A-Z => 26-51, 0-9 => 52-61, _ => 62
static int char_to_index(char c) {
    if (c >= 'a' && c <= 'z') return c - 'a';
    if (c >= 'A' && c <= 'Z') return 26 + (c - 'A');
    if (c >= '0' && c <= '9') return 52 + (c - '0');
    if (c == '_') return 62;
    if (c == ':') return 58;
    return -1;
}

TrieNode* name_table_init(void) {
    return create_node();
}

void name_table_free_recursive(TrieNode* node) {
    if (!node) return;
    for (int i = 0; i < ALPHABET_SIZE; ++i)
        name_table_free_recursive(node->children[i]);
    free(node);
}

void name_table_free(TrieNode* root) {
    name_table_free_recursive(root);
}

int name_table_insert(TrieNode *root, const char* name, Pid pid) {
    TrieNode* current = root;
    while (*name) {
        int index = char_to_index(*name);
        if (index == -1) {
            fprintf(stderr, "Invalid character in name: %c\n", *name);
            return -1;
        }

        if (!current->children[index])
            current->children[index] = create_node();
        current = current->children[index];
        name++;
    }
    current->is_end = 1;
    current->pid = pid;

    return 0;
}

Pid name_table_lookup(TrieNode *root, const char* name) {
    TrieNode* current = root;
    while (*name && current) {
        int index = char_to_index(*name);
        if (index == -1) return -1;
        current = current->children[index];
        name++;
    }
    return (current && current->is_end) ? current->pid : -1;
}

int name_table_remove_helper(TrieNode* node, const char* name) {
    if (!node) return 0;
    if (*name) {
        int index = char_to_index(*name);
        if (index == -1) return 0;
        if (name_table_remove_helper(node->children[index], name + 1)) {
            free(node->children[index]);
            node->children[index] = NULL;
        }
    } else {
        if (node->is_end) {
            node->is_end = 0;
            node->pid = -1;
            for (int i = 0; i < ALPHABET_SIZE; ++i)
                if (node->children[i]) return 0;
            return 1;
        }
    }
    return 0;
}

void name_table_remove(TrieNode *root, const char* name) {
    name_table_remove_helper(root, name);
}
