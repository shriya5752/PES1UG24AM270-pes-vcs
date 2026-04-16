// tree.h — Tree object interface

#ifndef TREE_H
#define TREE_H

#include "pes.h"
#include "index.h"  // ADD THIS LINE - so Index type is known

#define MAX_TREE_ENTRIES 1000

typedef struct {
    uint32_t mode;           // File mode (100644, 100755, 040000 for dir)
    char name[256];          // Entry name (no path separators)
    ObjectID hash;           // SHA-256 of the blob or subtree
} TreeEntry;

// Write a tree object from an array of entries
int tree_write(const TreeEntry *entries, int entry_count, ObjectID *id_out);

// Parse a tree object from raw data
int tree_parse(const void *data, size_t len, TreeEntry *entries_out, int *entry_count_out);

// Build a tree from the index
int tree_from_index(const Index *index, ObjectID *root_tree_out);

#endif // TREE_H