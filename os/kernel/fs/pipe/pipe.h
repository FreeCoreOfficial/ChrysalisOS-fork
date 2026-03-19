#pragma once

#include "../vfs/vnode.h"

#ifdef __cplusplus
extern "C" {
#endif

int pipe_create(vnode_t **out_read, vnode_t **out_write);

#ifdef __cplusplus
}
#endif
