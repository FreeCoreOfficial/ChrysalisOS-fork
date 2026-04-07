/* kernel/fs/pipe/pipe.c */
#include "pipe.h"
#include "../../mem/kmalloc.h"
#include "../../string.h"
#include "../vfs/fs_ops.h"

#define PIPE_LINUX_EAGAIN 11
#define PIPE_LINUX_EPIPE 32

#ifdef __x86_64__
#include "../../sched/task64.h"
#define yield_now task64_yield
#else
extern void yield(void);
#define yield_now yield
#endif

static int pipe_read_impl(vnode_t* node, uint32_t off, uint8_t* buf, uint32_t size,
                          int nonblocking) {
    (void)off;
    pipe_t* p = (pipe_t*)node->internal;
    uint32_t read = 0;
    
    while (read < size) {
        if (p->head == p->tail) {
            if (p->writer_count == 0 || read > 0) break;
            if (nonblocking) return -PIPE_LINUX_EAGAIN;
            yield_now(); // Simple block
            continue;
        }
        buf[read++] = p->buffer[p->tail];
        p->tail = (p->tail + 1) % PIPE_BUF_SIZE;
    }
    return (int)read;
}

static int pipe_write_impl(vnode_t* node, uint32_t off, const uint8_t* buf, uint32_t size,
                           int nonblocking) {
    (void)off;
    pipe_t* p = (pipe_t*)node->internal;
    uint32_t written = 0;
    
    while (written < size) {
        uint32_t next = (p->head + 1) % PIPE_BUF_SIZE;
        if (next == p->tail) {
            if (p->reader_count == 0) return -PIPE_LINUX_EPIPE; // Broken pipe
            if (nonblocking) return written > 0 ? (int)written : -PIPE_LINUX_EAGAIN;
            yield_now();
            continue;
        }
        p->buffer[p->head] = buf[written++];
        p->head = next;
    }
    return (int)written;
}

static int pipe_read(vnode_t* node, uint32_t off, uint8_t* buf, uint32_t size) {
    return pipe_read_impl(node, off, buf, size, 0);
}

static int pipe_write(vnode_t* node, uint32_t off, const uint8_t* buf, uint32_t size) {
    return pipe_write_impl(node, off, buf, size, 0);
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
    if (!node || !node->internal)
        return 0;
    pipe_t* p = (pipe_t*)node->internal;
    if (node->name && strcmp(node->name, "pipe-w") == 0) {
        if (p->writer_count > 0) p->writer_count--;
    } else {
        if (p->reader_count > 0) p->reader_count--;
    }
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
    static const char k_pipe_r_name[] = "pipe-r";
    static const char k_pipe_w_name[] = "pipe-w";
    n->name = is_writer ? k_pipe_w_name : k_pipe_r_name;
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

int pipe_is_vnode(vnode_t *node) {
    if (!node || !node->name)
        return 0;
    return strcmp(node->name, "pipe-r") == 0 || strcmp(node->name, "pipe-w") == 0;
}

int pipe_read_file(vnode_t *node, int nonblocking, uint8_t *buf, uint32_t size) {
    if (!pipe_is_vnode(node))
        return -1;
    return pipe_read_impl(node, 0, buf, size, nonblocking);
}

int pipe_write_file(vnode_t *node, int nonblocking, const uint8_t *buf, uint32_t size) {
    if (!pipe_is_vnode(node))
        return -1;
    return pipe_write_impl(node, 0, buf, size, nonblocking);
}
