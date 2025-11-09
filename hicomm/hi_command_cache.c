#include <stdlib.h>
#include <string.h>

// This is for performance purposes. We will keep caches of both the 
// inode table and blocks local that are read. FIFO - first in first out,
// so if a block is not used in a while, it will roll off fast, if its used
// it will move back to the first slot. This is a simple implementation of
// Linked Lists at their most basic, except that the evantual goal is to 
// manage these both in-memory and on-local-disk, so later when a block or
// inode reaches the end of the list, we will write it out, and then delete
// it as it ages.

// Define the inode structure
typedef struct inode {
    int id; // Inode number
    int mode; // File mode (permissions)
    int uid; // User ID of owner
    int gid; // Group ID of owner
    int size; // Size of file
    int atime; // Access time
    int mtime; // Modification time
    int ctime; // Change time
    // Add other fields as needed
} inode;

// Define the block structure with hash
typedef struct block {
    char data[4096];
    char hash[128];
} block;

// Define the cache node structure
typedef struct cache_node {
    void *data;
    struct cache_node *next;
} cache_node;

// Define the cache structure
typedef struct cache {
    cache_node *head;
    cache_node *tail;
} cache;

// Function to create a new cache node
cache_node *new_cache_node(void *data, size_t size) {
    cache_node *node = malloc(sizeof(cache_node));
    node->data = malloc(size);
    memcpy(node->data, data, size);
    node->next = NULL;
    return node;
}

// Function to access an element in the cache
void *cache_access(cache *c, int id) {
    cache_node *node = c->head;
    while (node != NULL) {
        if (((inode *)node->data)->id == id) {
            return node->data;
        }
        node = node->next;
    }
    return NULL;
}

// Function to delete an element from the cache
void cache_delete(cache *c, int id) {
    cache_node *node = c->head;
    cache_node *prev = NULL;
    while (node != NULL) {
        if (((inode *)node->data)->id == id) {
            if (prev == NULL) {
                c->head = node->next;
            } else {
                prev->next = node->next;
            }
            if (node == c->tail) {
                c->tail = prev;
            }
            free(node->data);
            free(node);
            return;
        }
        prev = node;
        node = node->next;
    }
}

// Function to update an element in the cache
void cache_update(cache *c, void *data, size_t size, int id) {
    cache_delete(c, id);
    cache_node *node = new_cache_node(data, size);
    if (c->head == NULL) {
        c->head = node;
    } else {
        c->tail->next = node;
    }
    c->tail = node;
}

// Function to search the inode cache
inode *inode_cache_search(cache *c, int id) {
    cache_node *node = c->head;
    while (node != NULL) {
        inode *i = (inode *)node->data;
        if (i->id == id) {
            return i;
        }
        node = node->next;
    }
    return NULL;
}

// Function to search the block cache
block *block_cache_search(cache *c, char *hash) {
    cache_node *node = c->head;
    while (node != NULL) {
        block *b = (block *)node->data;
        if (strcmp(b->hash, hash) == 0) {
            return b;
        }
        node = node->next;
    }
    return NULL;
}

