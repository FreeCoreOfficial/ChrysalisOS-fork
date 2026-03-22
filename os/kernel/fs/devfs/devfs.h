#pragma once

#include "../vfs/vnode.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
  DEV_TTY = 1,
  DEV_CONSOLE,
  DEV_NULL,
  DEV_ZERO,
  DEV_FB0,
  DEV_DRI_DIR,
  DEV_DRI_CARD0,
  DEV_INPUT_DIR,
  DEV_INPUT_EVENT0,
  DEV_INPUT_EVENT1
} dev_type_t;

typedef struct dev_node {
  vnode_t vnode;
  const char *name;
  dev_type_t type;
} dev_node_t;

vnode_t *devfs_root(void);

#ifdef __cplusplus
}
#endif
