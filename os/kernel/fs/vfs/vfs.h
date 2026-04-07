#pragma once
#include "vnode.h"
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define MAX_FILES_PER_PROCESS 64

typedef struct file {
  vnode_t *node;
  uint32_t offset;
  int flags;
  int refcount;
} file_t;

/* VFS Prototypes */
void vfs_mount(const char *path, vnode_t *root);
vnode_t *vfs_resolve(const char *path);

#ifdef __cplusplus
}
#endif
