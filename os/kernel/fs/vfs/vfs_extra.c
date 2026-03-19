#include "../../mem/kmalloc.h"
#include "../../string.h"
#include "fs_ops.h"
#include "vnode.h"

typedef struct ram_node {
  char name[32];
  uint8_t *data;
  uint32_t size;
  uint32_t capacity;
  struct ram_node *next;
} ram_node_t;

static ram_node_t *ram_files = NULL;

static int ram_open(vnode_t *node) {
  (void)node;
  return 0;
}

static int ram_close(vnode_t *node) {
  (void)node;
  return 0;
}

static uint32_t ram_poll(vnode_t *node, uint32_t events) {
  (void)node;
  uint32_t revents = 0;
  if (events & 0x001)
    revents |= 0x001;
  if (events & 0x004)
    revents |= 0x004;
  return revents;
}

static int ram_read(vnode_t *node, uint32_t off, uint8_t *buf, uint32_t size) {
  ram_node_t *rn = (ram_node_t *)node->internal;
  if (off >= rn->size)
    return 0;
  uint32_t to_read = size;
  if (off + size > rn->size)
    to_read = rn->size - off;
  memcpy(buf, rn->data + off, to_read);
  return to_read;
}

static int ram_write(vnode_t *node, uint32_t off, const uint8_t *buf,
                     uint32_t size) {
  ram_node_t *rn = (ram_node_t *)node->internal;
  if (off + size > rn->capacity) {
    // resize
    uint32_t new_cap = rn->capacity * 2;
    if (new_cap < off + size)
      new_cap = off + size + 1024;
    uint8_t *new_data = (uint8_t *)kmalloc(new_cap);
    if (rn->data) {
      memcpy(new_data, rn->data, rn->size);
      kfree(rn->data);
    }
    rn->data = new_data;
    rn->capacity = new_cap;
  }
  memcpy(rn->data + off, buf, size);
  if (off + size > rn->size)
    rn->size = off + size;
  return size;
}

static fs_ops_t ram_ops = {.open = ram_open,
                           .read = ram_read,
                           .write = ram_write,
                           .close = ram_close,
                           .poll = ram_poll,
                           .readdir = NULL};

vnode_t *vfs_create_ram_file(const char *name) {
  ram_node_t *rn = (ram_node_t *)kmalloc(sizeof(ram_node_t));
  strncpy(rn->name, name, 31);
  rn->size = 0;
  rn->capacity = 1024;
  rn->data = (uint8_t *)kmalloc(rn->capacity);
  rn->next = ram_files;
  ram_files = rn;

  vnode_t *vn = (vnode_t *)kmalloc(sizeof(vnode_t));
  vn->name = rn->name;
  vn->type = VNODE_FILE;
  vn->ops = &ram_ops;
  vn->internal = rn;
  vn->parent = NULL;

  // Register in some global list or mount it
  return vn;
}
