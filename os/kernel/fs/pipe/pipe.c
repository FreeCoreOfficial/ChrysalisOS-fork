#include "pipe.h"
#include "../vfs/fs_ops.h"
#include "../../mem/kmalloc.h"
#include "../../string.h"
#ifndef __x86_64__
#include "../../time/timer.h"
#endif

#define PIPE_BUF_SIZE 4096

typedef struct pipe_obj {
  uint8_t buf[PIPE_BUF_SIZE];
  uint32_t head;
  uint32_t tail;
  uint32_t len;
  int readers;
  int writers;
} pipe_obj_t;

typedef struct pipe_end {
  pipe_obj_t *pipe;
  int is_read;
} pipe_end_t;

static fs_ops_t pipe_ops;

static void pipe_wait(void) {
#ifdef __x86_64__
  for (volatile int i = 0; i < 100000; i++)
    asm volatile("");
#else
  sleep(1);
#endif
}

static int pipe_open(struct vnode *n) {
  (void)n;
  return 0;
}

static int pipe_close(struct vnode *n) {
  if (!n || !n->internal)
    return 0;
  pipe_end_t *end = (pipe_end_t *)n->internal;
  pipe_obj_t *p = end->pipe;
  if (p) {
    if (end->is_read)
      p->readers--;
    else
      p->writers--;
    if (p->readers <= 0 && p->writers <= 0) {
      kfree(p);
    }
  }
  kfree(end);
  kfree(n);
  return 0;
}

static int pipe_read(struct vnode *n, uint32_t off, uint8_t *buf,
                     uint32_t size) {
  (void)off;
  if (!n || !buf || size == 0)
    return 0;
  pipe_end_t *end = (pipe_end_t *)n->internal;
  if (!end || !end->is_read)
    return -1;
  pipe_obj_t *p = end->pipe;
  if (!p)
    return -1;

  while (p->len == 0 && p->writers > 0) {
    pipe_wait();
  }
  if (p->len == 0 && p->writers <= 0)
    return 0;

  uint32_t to_read = (size < p->len) ? size : p->len;
  for (uint32_t i = 0; i < to_read; i++) {
    buf[i] = p->buf[p->tail];
    p->tail = (p->tail + 1) % PIPE_BUF_SIZE;
  }
  p->len -= to_read;
  return (int)to_read;
}

static int pipe_write(struct vnode *n, uint32_t off, const uint8_t *buf,
                      uint32_t size) {
  (void)off;
  if (!n || !buf || size == 0)
    return 0;
  pipe_end_t *end = (pipe_end_t *)n->internal;
  if (!end || end->is_read)
    return -1;
  pipe_obj_t *p = end->pipe;
  if (!p)
    return -1;
  if (p->readers <= 0)
    return -1;

  uint32_t written = 0;
  while (written < size) {
    while (p->len >= PIPE_BUF_SIZE && p->readers > 0) {
      pipe_wait();
    }
    if (p->readers <= 0)
      break;
    if (p->len >= PIPE_BUF_SIZE)
      break;
    p->buf[p->head] = buf[written++];
    p->head = (p->head + 1) % PIPE_BUF_SIZE;
    p->len++;
  }
  return (int)written;
}

static uint32_t pipe_poll(struct vnode *n, uint32_t events) {
  if (!n || !n->internal)
    return 0;
  pipe_end_t *end = (pipe_end_t *)n->internal;
  pipe_obj_t *p = end->pipe;
  if (!p)
    return 0;
  uint32_t revents = 0;
  if (end->is_read) {
    if ((events & 0x001) && p->len > 0)
      revents |= 0x001;
  } else {
    if ((events & 0x004) && p->len < PIPE_BUF_SIZE && p->readers > 0)
      revents |= 0x004;
  }
  return revents;
}

int pipe_create(vnode_t **out_read, vnode_t **out_write) {
  if (!out_read || !out_write)
    return -1;
  pipe_obj_t *p = (pipe_obj_t *)kmalloc(sizeof(pipe_obj_t));
  if (!p)
    return -1;
  memset(p, 0, sizeof(*p));
  p->readers = 1;
  p->writers = 1;

  pipe_end_t *read_end = (pipe_end_t *)kmalloc(sizeof(pipe_end_t));
  pipe_end_t *write_end = (pipe_end_t *)kmalloc(sizeof(pipe_end_t));
  if (!read_end || !write_end) {
    if (read_end)
      kfree(read_end);
    if (write_end)
      kfree(write_end);
    kfree(p);
    return -1;
  }
  read_end->pipe = p;
  read_end->is_read = 1;
  write_end->pipe = p;
  write_end->is_read = 0;

  vnode_t *rvn = (vnode_t *)kmalloc(sizeof(vnode_t));
  vnode_t *wvn = (vnode_t *)kmalloc(sizeof(vnode_t));
  if (!rvn || !wvn) {
    if (rvn)
      kfree(rvn);
    if (wvn)
      kfree(wvn);
    kfree(read_end);
    kfree(write_end);
    kfree(p);
    return -1;
  }

  memset(rvn, 0, sizeof(*rvn));
  memset(wvn, 0, sizeof(*wvn));
  rvn->name = "pipe";
  rvn->type = VNODE_DEV;
  rvn->ops = &pipe_ops;
  rvn->internal = read_end;

  wvn->name = "pipe";
  wvn->type = VNODE_DEV;
  wvn->ops = &pipe_ops;
  wvn->internal = write_end;

  pipe_ops.open = pipe_open;
  pipe_ops.read = pipe_read;
  pipe_ops.write = pipe_write;
  pipe_ops.close = pipe_close;
  pipe_ops.poll = pipe_poll;
  pipe_ops.readdir = NULL;

  *out_read = rvn;
  *out_write = wvn;
  return 0;
}
