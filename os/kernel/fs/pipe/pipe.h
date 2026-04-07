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
int pipe_create(vnode_t **rnode, vnode_t **wnode);
int pipe_is_vnode(vnode_t *node);
int pipe_read_file(vnode_t *node, int nonblocking, uint8_t *buf, uint32_t size);
int pipe_write_file(vnode_t *node, int nonblocking, const uint8_t *buf, uint32_t size);

#ifdef __cplusplus
}
#endif
