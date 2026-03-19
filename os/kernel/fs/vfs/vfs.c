#include "mount.h"
#include "vnode.h"
#include "fs_ops.h"
#include "../../mem/kmalloc.h"
#include "../../string.h"
#include <stdint.h>

#define MAX_MOUNTS 8

typedef struct mount {
    char* path;       /* mount point string (e.g. "/") */
    uint32_t len;
    vnode_t* root;    /* root vnode of the mounted FS */
} mount_t;

static mount_t mounts[MAX_MOUNTS];
static int mount_count = 0;

static char* vfs_strdup(const char* s) {
    if (!s)
        return NULL;
    size_t len = strlen(s);
    char* out = (char*)kmalloc(len + 1);
    if (!out)
        return NULL;
    memcpy(out, s, len);
    out[len] = 0;
    return out;
}

void vfs_mount(const char* path, vnode_t* root) {
    if (!path || !root)
        return;

    for (int i = 0; i < mount_count; i++) {
        if (mounts[i].path && strcmp(mounts[i].path, path) == 0) {
            mounts[i].root = root;
            return;
        }
    }

    if (mount_count >= MAX_MOUNTS)
        return;

    mounts[mount_count].path = vfs_strdup(path);
    mounts[mount_count].len = (uint32_t)strlen(path);
    mounts[mount_count].root = root;
    mount_count++;
}

static mount_t* vfs_find_mount(const char* path) {
    mount_t* best = NULL;
    if (!path)
        return NULL;

    for (int i = 0; i < mount_count; i++) {
        mount_t* m = &mounts[i];
        if (!m->path || !m->root)
            continue;
        uint32_t len = m->len;
        if (len == 0)
            continue;
        if (strncmp(path, m->path, len) != 0)
            continue;
        if (path[len] != 0 && path[len] != '/')
            continue;
        if (!best || len > best->len)
            best = m;
    }
    return best;
}

static vnode_t* vfs_find_child(vnode_t* dir, const char* name, size_t len) {
    if (!dir || !dir->ops || !dir->ops->readdir)
        return NULL;

    uint32_t idx = 0;
    while (1) {
        vnode_t* out = NULL;
        int res = dir->ops->readdir(dir, idx, &out);
        if (res <= 0 || !out)
            break;
        if (out->name && strlen(out->name) == len &&
            strncmp(out->name, name, len) == 0) {
            return out;
        }
        idx++;
    }
    return NULL;
}

vnode_t* vfs_resolve(const char* path) {
    if (!path)
        return NULL;
    if (path[0] != '/')
        return NULL;

    mount_t* m = vfs_find_mount(path);
    if (!m || !m->root)
        return NULL;

    const char* sub = path + m->len;
    if (m->len == 1)
        sub = path + 1;
    if (*sub == '/')
        sub++;
    if (*sub == 0)
        return m->root;

    vnode_t* cur = m->root;
    const char* p = sub;
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

        vnode_t* child = vfs_find_child(cur, start, len);
        if (!child)
            return NULL;
        cur = child;
    }
    return cur;
}
