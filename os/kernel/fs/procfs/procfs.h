#pragma once

#include "../vfs/vnode.h"

#ifdef __cplusplus
extern "C" {
#endif

vnode_t *procfs_root(void);

#ifdef __cplusplus
}
#endif
