/* kernel/fs/pipe/pipe.c */
#include "pipe.h"
#include "../../mem/kmalloc.h"
#include "../../string.h"
#include "../vfs/fs_ops.h"

#ifdef __x86_64__
#include "../../sched/task64.h"
#define yield_now task64_yield
#else
extern void yield(void);
#define yield_now yield
#endif

static int pipe_read(vnode_t* node, uint32_t off, uint8_t* buf, uint32_t size) {
    (void)off;
    pipe_t* p = (pipe_t*)node->internal;
    uint32_t read = 0;
    
    while (read < size) {
        if (p->head == p->tail) {
            if (p->writer_count == 0 || read > 0) break;
            yield_now(); // Simple block
            continue;
        }
        buf[read++] = p->buffer[p->tail];
        p->tail = (p->tail + 1) % PIPE_BUF_SIZE;
    }
    return (int)read;
}

static int pipe_write(vnode_t* node, uint32_t off, const uint8_t* buf, uint32_t size) {
    (void)off;
    pipe_t* p = (pipe_t*)node->internal;
    uint32_t written = 0;
    
    while (written < size) {
        uint32_t next = (p->head + 1) % PIPE_BUF_SIZE;
        if (next == p->tail) {
            if (p->reader_count == 0) return -1; // Broken pipe
            yield_now();
            continue;
        }
        p->buffer[p->head] = buf[written++];
        p->head = next;
    }
    return (int)written;
}

static uint32_t pipe_poll(vnode_t* node, uint32_t events) {
    pipe_t* p = (pipe_t*)node->internal;
    uint32_t revents = 0;
    if (events & 0x001) { // POLLIN
        if (p->head != p->tail || p->writer_count == 0) revents |= 0x001;
    }
    if (events & 0x004) { // POLLOUT
        uint32_t next = (p->head + 1) % PIPE_BUF_SIZE;
        if (next != p->tail || p->reader_count == 0) revents |= 0x004;
    }
    return revents;
}

static int pipe_close(vnode_t* node) {
    (void)node;
    return 0; 
}

static fs_ops_t pipe_ops = {
    .read = (int (*)(struct vnode *, uint32_t, uint8_t *, uint32_t))pipe_read,
    .write = (int (*)(struct vnode *, uint32_t, const uint8_t *, uint32_t))pipe_write,
    .poll = pipe_poll,
    .close = pipe_close
};

vnode_t* pipe_create_vnode(pipe_t* p, int is_writer) {
    vnode_t* n = (vnode_t*)kmalloc(sizeof(vnode_t));
    memset(n, 0, sizeof(*n));
    n->type = VNODE_FILE;
    n->ops = &pipe_ops;
    n->internal = p;
    if (is_writer) p->writer_count++;
    else p->reader_count++;
    return n;
}

int pipe_create(vnode_t **rnode, vnode_t **wnode) {
    pipe_t* p = (pipe_t*)kmalloc(sizeof(pipe_t));
    memset(p, 0, sizeof(*p));
    *rnode = pipe_create_vnode(p, 0);
    *wnode = pipe_create_vnode(p, 1);
    return 0;
}
