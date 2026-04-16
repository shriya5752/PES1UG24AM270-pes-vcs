// tree.c — Tree object implementation

#include "tree.h"
#include "index.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

// Forward declaration
int object_write(ObjectType type, const void *data, size_t len, ObjectID *id_out);

// Compare function for sorting tree entries
static int compare_tree_entries(const void *a, const void *b) {
    const TreeEntry *ea = (const TreeEntry *)a;
    const TreeEntry *eb = (const TreeEntry *)b;
    return strcmp(ea->name, eb->name);
}

// Write a tree object to the object store
int tree_write(const TreeEntry *entries, int entry_count, ObjectID *id_out) {
    if (entry_count == 0) {
        return -1;
    }
    
    // First, sort entries by name
    TreeEntry *sorted = malloc(entry_count * sizeof(TreeEntry));
    if (!sorted) return -1;
    memcpy(sorted, entries, entry_count * sizeof(TreeEntry));
    qsort(sorted, entry_count, sizeof(TreeEntry), compare_tree_entries);
    
    // Calculate total size
    size_t total_size = 0;
    for (int i = 0; i < entry_count; i++) {
        total_size += 4 + strlen(sorted[i].name) + 1 + HASH_SIZE;
    }
    
    // Build the tree data
    uint8_t *data = malloc(total_size);
    if (!data) {
        free(sorted);
        return -1;
    }
    
    uint8_t *p = data;
    for (int i = 0; i < entry_count; i++) {
        // Write mode (4 bytes, network byte order)
        uint32_t mode_be = (sorted[i].mode >> 24) | 
                           ((sorted[i].mode >> 8) & 0xFF00) |
                           ((sorted[i].mode << 8) & 0xFF0000) |
                           (sorted[i].mode << 24);
        memcpy(p, &mode_be, 4);
        p += 4;
        
        // Write name (null-terminated)
        size_t name_len = strlen(sorted[i].name) + 1;
        memcpy(p, sorted[i].name, name_len);
        p += name_len;
        
        // Write hash
        memcpy(p, sorted[i].hash.hash, HASH_SIZE);
        p += HASH_SIZE;
    }
    
    free(sorted);
    
    // Write to object store
    int result = object_write(OBJ_TREE, data, total_size, id_out);
    free(data);
    return result;
}

// Parse a tree object from raw data
int tree_parse(const void *data, size_t len, TreeEntry *entries_out, int *entry_count_out) {
    const uint8_t *p = (const uint8_t*)data;
    const uint8_t *end = p + len;
    int count = 0;
    
    while (p < end && count < MAX_TREE_ENTRIES) {
        // Read mode
        uint32_t mode_be;
        memcpy(&mode_be, p, 4);
        entries_out[count].mode = (mode_be >> 24) |
                                  ((mode_be >> 8) & 0xFF00) |
                                  ((mode_be << 8) & 0xFF0000) |
                                  (mode_be << 24);
        p += 4;
        
        // Read name
        size_t name_len = strlen((const char*)p) + 1;
        if (name_len > sizeof(entries_out[count].name)) return -1;
        memcpy(entries_out[count].name, p, name_len);
        p += name_len;
        
        // Read hash
        memcpy(entries_out[count].hash.hash, p, HASH_SIZE);
        p += HASH_SIZE;
        
        count++;
    }
    
    *entry_count_out = count;
    return 0;
}

// Build a tree from the index
int tree_from_index(const Index *index, ObjectID *root_tree_out) {
    if (index->count == 0) {
        fprintf(stderr, "error: index is empty\n");
        return -1;
    }
    
    // Build tree entries from index
    TreeEntry entries[MAX_TREE_ENTRIES];
    int entry_count = 0;
    
    for (int i = 0; i < index->count; i++) {
        const IndexEntry *ie = &index->entries[i];
        
        // Get basename (for now, just use the full path as name)
        // In a real implementation, you'd handle directories
        const char *name = ie->path;
        
        strncpy(entries[entry_count].name, name, sizeof(entries[entry_count].name) - 1);
        entries[entry_count].name[sizeof(entries[entry_count].name) - 1] = '\0';
        entries[entry_count].mode = ie->mode;
        entries[entry_count].hash = ie->hash;
        entry_count++;
    }
    
    // Write the tree object
    return tree_write(entries, entry_count, root_tree_out);
}