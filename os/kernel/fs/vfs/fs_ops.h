#pragma once

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

struct vnode;

typedef struct fs_ops {
    /* Return 0 on success, negative on error, or bytes for read/write */
    int (*open)(struct vnode* node);
    int (*read)(struct vnode* node, uint32_t off, uint8_t* buf, uint32_t size);
    int (*write)(struct vnode* node, uint32_t off, const uint8_t* buf, uint32_t size);
    int (*close)(struct vnode* node);
    uint32_t (*poll)(struct vnode* node, uint32_t events);
    int (*ioctl)(struct vnode* node, uint32_t cmd, void* arg);

    /* readdir: for directory nodes; index starts at 0. If no more entries return 0 and set *out = NULL.
       On success return 1 and set *out to the vnode pointer (owned by FS). */
    int (*readdir)(struct vnode* dir, uint32_t index, struct vnode** out);

    /* readlink: for symlink nodes. Returns number of bytes copied to buf (excluding null), or negative on error. */
    int (*readlink)(struct vnode* node, char* buf, uint32_t bufsize);
} fs_ops_t;


#ifdef __cplusplus
}
#endif
