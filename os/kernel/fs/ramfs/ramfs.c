#include "ramfs.h"
#include "../vfs/fs_ops.h"
#include "../vfs/vnode.h"
#include "../../mem/kmalloc.h"
#include "../../string.h"
#include <stdint.h>

static fs_ops_t ramfs_ops;

static vnode_t* ramfs_vnode_for(FSNode* node) {
    if (!node)
        return NULL;
    if (node->vnode)
        return node->vnode;

    vnode_t* vn = (vnode_t*)kmalloc(sizeof(vnode_t));
    if (!vn)
        return NULL;

    vn->name = node->name;
    vn->type = (node->flags == FS_DIR) ? VNODE_DIR : VNODE_FILE;
    vn->ops = &ramfs_ops;
    vn->internal = node;
    vn->parent = node->parent ? ramfs_vnode_for(node->parent) : NULL;
    node->vnode = vn;
    return vn;
}

static int ramfs_open(struct vnode* n) {
    (void)n;
    return 0;
}

static int ramfs_close(struct vnode* n) {
    (void)n;
    return 0;
}

static uint32_t ramfs_poll(struct vnode* n, uint32_t events) {
    if (!n)
        return 0;
    uint32_t revents = 0;
    if ((events & 0x001) != 0) /* EPOLLIN/POLLIN */
        revents |= 0x001;
    if ((events & 0x004) != 0) /* EPOLLOUT/POLLOUT */
        revents |= 0x004;
    return revents;
}

static int ramfs_read(struct vnode* n, uint32_t off, uint8_t* buf, uint32_t size) {
    if (!n || !buf || size == 0)
        return 0;
    FSNode* node = (FSNode*)n->internal;
    if (!node || node->flags != FS_FILE || !node->data)
        return 0;
    if (off >= node->length)
        return 0;
    uint32_t to_read = size;
    if (off + size > node->length)
        to_read = (uint32_t)(node->length - off);
    memcpy(buf, (uint8_t*)node->data + off, to_read);
    return (int)to_read;
}

static int ramfs_write(struct vnode* n, uint32_t off, const uint8_t* buf, uint32_t size) {
    if (!n || !buf || size == 0)
        return 0;
    FSNode* node = (FSNode*)n->internal;
    if (!node || node->flags != FS_FILE)
        return -1;

    size_t required = (size_t)off + size;
    if (!node->is_dynamic) {
        size_t cap = node->length;
        if (required > cap)
            cap = required;
        if (cap < 256)
            cap = 256;
        void* newbuf = kmalloc(cap);
        if (!newbuf)
            return -1;
        if (node->data && node->length)
            memcpy(newbuf, node->data, node->length);
        node->data = newbuf;
        node->capacity = cap;
        node->is_dynamic = 1;
    } else if (required > node->capacity) {
        size_t new_cap = node->capacity ? node->capacity : 256;
        while (new_cap < required)
            new_cap *= 2;
        void* newbuf = kmalloc(new_cap);
        if (!newbuf)
            return -1;
        if (node->data && node->length)
            memcpy(newbuf, node->data, node->length);
        if (node->data)
            kfree(node->data);
        node->data = newbuf;
        node->capacity = new_cap;
    }

    memcpy((uint8_t*)node->data + off, buf, size);
    if (required > node->length)
        node->length = required;
    return (int)size;
}

static int ramfs_readdir(struct vnode* dir, uint32_t index, struct vnode** out) {
    if (!dir || !out)
        return 0;
    FSNode* node = (FSNode*)dir->internal;
    if (!node || node->flags != FS_DIR)
        return 0;

    FSNode* child = node->children;
    uint32_t i = 0;
    while (child && i < index) {
        child = child->next;
        i++;
    }
    if (!child) {
        *out = NULL;
        return 0;
    }
    *out = ramfs_vnode_for(child);
    return *out ? 1 : 0;
}

static fs_ops_t ramfs_ops = {
    .open = ramfs_open,
    .read = ramfs_read,
    .write = ramfs_write,
    .close = ramfs_close,
    .poll = ramfs_poll,
    .readdir = ramfs_readdir
};

struct vnode* ramfs_root(void) {
    return ramfs_vnode_for(ramfs_fs_root());
}
