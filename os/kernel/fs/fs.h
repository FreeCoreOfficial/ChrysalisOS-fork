#pragma once

#include <stddef.h>  /* for size_t */


#define FS_FILE 1
#define FS_DIR  2

struct vnode;

typedef struct FSNode {
    char name[256];          /* Filename */
    void* data;              /* Pointer to file data */
    size_t length;           /* File size in bytes */
    size_t capacity;         /* Allocated capacity (for dynamic files) */
    int is_dynamic;          /* 1 if data is heap-allocated and writable */
    int flags;               /* FS_FILE or FS_DIR */
    struct FSNode* next;     /* Next sibling */
    struct FSNode* children; /* First child (for directories) */
    struct FSNode* parent;   /* Parent node */
    struct vnode* vnode;     /* VFS vnode mapping (lazy) */
} FSNode;

void fs_init();
void fs_create(const char* name, const char* content);
void fs_list();
const FSNode* fs_find(const char* name);

/* RAMFS helper: Find file in RAMFS and return its data pointer and size */
const void* ramfs_read_file(const char* name, size_t* out_size);
