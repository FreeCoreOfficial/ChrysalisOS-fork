/* kernel/fs/ramfs/ramfs_add.c */
#include "../../fs/fs.h"
#include "../../mem/kmalloc.h"
#include "../../string.h"
#include <stddef.h>

static FSNode ramfs_file_root = {0};

struct FSNode* ramfs_fs_root(void) {
    if (ramfs_file_root.name[0] == 0) {
        memset(&ramfs_file_root, 0, sizeof(FSNode));
        strcpy(ramfs_file_root.name, "/");
        ramfs_file_root.flags = FS_DIR;
        ramfs_file_root.children = NULL;
        ramfs_file_root.parent = NULL;
        ramfs_file_root.vnode = NULL;
        ramfs_file_root.is_dynamic = 0;
        ramfs_file_root.capacity = 0;
    }
    return &ramfs_file_root;
}

struct FSNode* ramfs_find_child(struct FSNode* dir, const char* name) {
    if (!dir || dir->flags != FS_DIR || !name)
        return NULL;
    FSNode* node = dir->children;
    while (node) {
        if (strcmp(node->name, name) == 0)
            return node;
        node = node->next;
    }
    return NULL;
}

static struct FSNode* ramfs_add_child(struct FSNode* dir, const char* name,
                                      int flags, const void* data, size_t len,
                                      int dynamic) {
    if (!dir || dir->flags != FS_DIR || !name || name[0] == 0)
        return NULL;

    FSNode* existing = ramfs_find_child(dir, name);
    if (existing)
        return existing;

    FSNode* node = (FSNode*)kmalloc(sizeof(FSNode));
    if (!node)
        return NULL;
    memset(node, 0, sizeof(FSNode));
    strncpy(node->name, name, sizeof(node->name) - 1);
    node->name[sizeof(node->name) - 1] = 0;
    node->flags = flags;
    node->parent = dir;
    node->children = NULL;
    node->vnode = NULL;
    node->next = dir->children;
    dir->children = node;

    if (flags == FS_FILE) {
        node->length = len;
        node->capacity = len;
        node->is_dynamic = dynamic ? 1 : 0;
        node->data = (void*)data;
        if (dynamic) {
            size_t cap = (len > 0) ? len : 256;
            void* buf = kmalloc(cap);
            if (!buf) {
                return node;
            }
            if (data && len)
                memcpy(buf, data, len);
            node->data = buf;
            node->capacity = cap;
        }
    }

    return node;
}

struct FSNode* ramfs_mkdir_at(struct FSNode* dir, const char* name) {
    struct FSNode* node = ramfs_add_child(dir, name, FS_DIR, NULL, 0, 0);
    if (node && node->flags != FS_DIR)
        return NULL;
    return node;
}

struct FSNode* ramfs_create_file_at(struct FSNode* dir, const char* name,
                                    const void* data, size_t len, int dynamic) {
    struct FSNode* node = ramfs_add_child(dir, name, FS_FILE, data, len, dynamic);
    if (node && node->flags != FS_FILE)
        return NULL;
    return node;
}

int ramfs_unlink_node(struct FSNode* node) {
    if (!node || !node->parent)
        return -1;
    if (node->flags == FS_DIR && node->children)
        return -1;

    FSNode* prev = NULL;
    FSNode* cur = node->parent->children;
    while (cur) {
        if (cur == node) {
            if (prev)
                prev->next = cur->next;
            else
                node->parent->children = cur->next;
            break;
        }
        prev = cur;
        cur = cur->next;
    }

    if (node->flags == FS_FILE && node->is_dynamic && node->data) {
        kfree(node->data);
    }
    kfree(node);
    return 0;
}

struct FSNode* ramfs_find_path_node(const char* path) {
    if (!path)
        return NULL;
    FSNode* cur = ramfs_fs_root();
    if (!cur)
        return NULL;

    const char* p = path;
    if (*p == '/')
        p++;

    while (*p) {
        while (*p == '/')
            p++;
        if (!*p)
            break;
        const char* start = p;
        while (*p && *p != '/')
            p++;
        size_t len = (size_t)(p - start);
        if (len == 0)
            continue;
        if (len == 1 && start[0] == '.')
            continue;
        if (len == 2 && start[0] == '.' && start[1] == '.') {
            if (cur->parent)
                cur = cur->parent;
            continue;
        }
        char name[256];
        if (len >= sizeof(name))
            return NULL;
        memcpy(name, start, len);
        name[len] = 0;
        
        cur = ramfs_find_child(cur, name);
        if (!cur) {
            return NULL;
        }
    }
    return cur;
}

static int ramfs_split_parent(const char* path, FSNode** out_dir, char* out_name,
                              size_t out_name_size) {
    if (!path || !out_dir || !out_name || out_name_size == 0 || path[0] != '/')
        return -1;

    FSNode* cur = ramfs_fs_root();
    if (!cur)
        return -1;

    const char* p = path + 1;
    while (*p == '/')
        p++;
    if (!*p)
        return -1;

    while (*p) {
        const char* start = p;
        while (*p && *p != '/')
            p++;
        size_t len = (size_t)(p - start);
        if (len == 0)
            return -1;

        while (*p == '/')
            p++;
        if (!*p) {
            if (len >= out_name_size)
                return -1;
            memcpy(out_name, start, len);
            out_name[len] = 0;
            *out_dir = cur;
            return 0;
        }

        if (len == 1 && start[0] == '.')
            continue;
        if (len == 2 && start[0] == '.' && start[1] == '.') {
            if (cur->parent)
                cur = cur->parent;
            continue;
        }

        char name[256];
        if (len >= sizeof(name))
            return -1;
        memcpy(name, start, len);
        name[len] = 0;
        cur = ramfs_find_child(cur, name);
        if (!cur || cur->flags != FS_DIR)
            return -1;
    }

    return -1;
}

int ramfs_link_path(const char* oldpath, const char* newpath) {
    FSNode* src = ramfs_find_path_node(oldpath);
    if (!src || src->flags != FS_FILE)
        return -1;

    FSNode* dir = NULL;
    char name[256];
    if (ramfs_split_parent(newpath, &dir, name, sizeof(name)) < 0)
        return -1;
    if (!dir || dir->flags != FS_DIR)
        return -1;
    if (ramfs_find_child(dir, name))
        return -2;

    return ramfs_create_file_at(dir, name, src->data, src->length, 1) ? 0 : -1;
}



void ramfs_create_file(const char* name, const void* data, size_t len) {
    FSNode* root = ramfs_fs_root();
    if (!root || !name)
        return;
        
    const char* p = name;

    if (*p == '/')
        p++;


    FSNode* dir = root;
    const char* last = p;
    const char* it = p;
    while (*it) {
        if (*it == '/') {
            size_t len_part = (size_t)(it - last);
            if (len_part > 0) {
                char part[256];
                if (len_part >= sizeof(part))
                    return;
                memcpy(part, last, len_part);
                part[len_part] = 0;
                FSNode* child = ramfs_find_child(dir, part);
                if (!child)
                    child = ramfs_mkdir_at(dir, part);
                if (!child || child->flags != FS_DIR)
                    return;
                dir = child;
            }
            last = it + 1;
        }
        it++;
    }

    const char* filename = last;
    if (!filename || filename[0] == 0)
        return;

    FSNode* existing = ramfs_find_child(dir, filename);
    if (existing && existing->flags == FS_FILE) {
        if (existing->is_dynamic && existing->data)
            kfree(existing->data);
        existing->data = (void*)data;
        existing->length = len;
        existing->capacity = len;
        existing->is_dynamic = 0;
        return;
    }

    ramfs_create_file_at(dir, filename, data, len, 0);
}

const void* ramfs_read_file(const char* name, size_t* out_size) {
    FSNode* node = ramfs_find_path_node(name);
    if (!node || node->flags != FS_FILE)
        return NULL;
    if (out_size)
        *out_size = node->length;
    return node->data;
}
