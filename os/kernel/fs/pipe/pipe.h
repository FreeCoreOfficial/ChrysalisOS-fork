/* kernel/fs/pipe/pipe.h */
#pragma once
#include "../vfs/vnode.h"
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define PIPE_BUF_SIZE 4096

typedef struct pipe {
    uint8_t buffer[PIPE_BUF_SIZE];
    uint32_t head;
    uint32_t tail;
    int reader_count;
    int writer_count;
} pipe_t;

vnode_t* pipe_create_vnode(pipe_t* p, int is_writer);

#ifdef __cplusplus
}
#endif
