#pragma once

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    VNODE_FILE,
    VNODE_DIR,
    VNODE_DEV
} vnode_type_t;

struct fs_ops;

/* generic VFS node */
typedef struct vnode {
    const char* name;         /* pointer to a static string (no heap) */
    vnode_type_t type;

    struct fs_ops* ops;      /* pointer to operations implemented by FS */
    void* internal;          /* FS-private data */

    struct vnode* parent;    /* parent vnode (or NULL for root) */
    uint64_t size;           /* file size (bytes), if known */
} vnode_t;

#ifdef __cplusplus
}
#endif
