#include "devfs.h"
#include "../vfs/fs_ops.h"
#include "../../mem/kmalloc.h"
#include "../../string.h"
#include "../../video/kms.h"
#ifndef __x86_64__
#include "../../terminal.h"
#include "../../input/keyboard_buffer.h"
#include "../../video/framebuffer.h"
#else
#include "../../drivers/serial.h"
#endif
#ifndef __x86_64__
#include "../../time/timer.h"
#endif

static fs_ops_t devfs_ops;
static dev_node_t dev_root;
static dev_node_t dev_tty;
static dev_node_t dev_null;
static dev_node_t dev_zero;
static dev_node_t dev_fb0;
static dev_node_t dev_dri;
static dev_node_t dev_card0;
static dev_node_t dev_input;
static dev_node_t dev_event0;
static dev_node_t dev_event1;
static dev_node_t dev_console;

#include "../../input/input.h"
#include "../../linux_compat/linux_abi.h"
#ifdef __x86_64__
#include "../../linux_compat/linux_syscall_x86_64.h"
#define copy_to_user(d, s, n) (memcpy(d, s, n), 0)
#else
extern int copy_to_user(void *dst, const void *src, uint32_t size);
#endif

static int devfs_open(struct vnode *n) {
  (void)n;
  return 0;
}

static int devfs_close(struct vnode *n) {
  (void)n;
  return 0;
}

static int devfs_ioctl(struct vnode *n, uint32_t cmd, void *arg) {
  if (!n)
    return -1;
  dev_node_t *dn = (dev_node_t *)n;
  if (dn->type == DEV_DRI_CARD0) {
    return kms_ioctl(cmd, arg);
  }
  return -1;
}

static int devfs_read(struct vnode *n, uint32_t off, uint8_t *buf,
                      uint32_t size) {
  (void)off;
  if (!n || !buf || size == 0)
    return 0;
  dev_node_t *dn = (dev_node_t *)n;
  switch (dn->type) {
  case DEV_CONSOLE:
  case DEV_TTY: {
    uint32_t read = 0;
    while (read < size) {
      while (
#ifdef __x86_64__
          !serial_received()
#else
          !kbd_has_char()
#endif
      ) {
#ifdef __x86_64__
        for (volatile int i = 0; i < 100000; i++)
          asm volatile("");
#else
        sleep(1);
#endif
      }
      char c =
#ifdef __x86_64__
          serial_read();
#else
          kbd_get_char();
#endif
      if (!c)
        break;
      buf[read++] = (uint8_t)c;
      if (c == '\n')
        break;
    }
    return (int)read;
  }
  case DEV_NULL:
    return 0;
  case DEV_ZERO:
    memset(buf, 0, size);
    return (int)size;
  case DEV_FB0: {
#ifndef __x86_64__
    uint32_t w = 0, h = 0, pitch = 0;
    uint8_t bpp = 0;
    uint8_t *fb = NULL;
    fb_get_info(&w, &h, &pitch, &bpp, &fb);
    if (!fb || pitch == 0 || h == 0)
      return 0;
    uint32_t fb_size = pitch * h;
    if (off >= fb_size)
      return 0;
    uint32_t to_copy = size;
    if (off + to_copy > fb_size)
      to_copy = fb_size - off;
    memcpy(buf, fb + off, to_copy);
    return (int)to_copy;
#else
    (void)off;
    return 0;
#endif
  }
  case DEV_DRI_CARD0:
    return 0;
  case DEV_DRI_DIR:
    return 0;
  case DEV_INPUT_EVENT0:
  case DEV_INPUT_EVENT1: {
    struct linux_input_event ev;
    if (size < sizeof(ev)) return 0;
    if (input_pop_evdev(&ev)) {
      memcpy(buf, &ev, sizeof(ev));
      return (int)sizeof(ev);
    }
    return 0;
  }
  default:
    return 0;
  }
}

static int devfs_write(struct vnode *n, uint32_t off, const uint8_t *buf,
                       uint32_t size) {
  (void)off;
  if (!n || !buf || size == 0)
    return 0;
  dev_node_t *dn = (dev_node_t *)n;
  switch (dn->type) {
  case DEV_TTY:
    for (uint32_t i = 0; i < size; i++) {
#ifdef __x86_64__
      serial_write((char)buf[i]);
#else
      terminal_putchar((char)buf[i]);
#endif
    }
    return (int)size;
  case DEV_NULL:
    return (int)size;
  case DEV_ZERO:
    return (int)size;
  case DEV_FB0: {
#ifndef __x86_64__
    uint32_t w = 0, h = 0, pitch = 0;
    uint8_t bpp = 0;
    uint8_t *fb = NULL;
    fb_get_info(&w, &h, &pitch, &bpp, &fb);
    if (!fb || pitch == 0 || h == 0)
      return 0;
    uint32_t fb_size = pitch * h;
    if (off >= fb_size)
      return 0;
    uint32_t to_copy = size;
    if (off + to_copy > fb_size)
      to_copy = fb_size - off;
    memcpy(fb + off, buf, to_copy);
    return (int)to_copy;
#else
    (void)off;
    return 0;
#endif
  }
  case DEV_DRI_CARD0:
    return (int)size;
  case DEV_DRI_DIR:
    return 0;
  default:
    return 0;
  }
}

static uint32_t devfs_poll(struct vnode *n, uint32_t events) {
  if (!n)
    return 0;
  dev_node_t *dn = (dev_node_t *)n;
  uint32_t revents = 0;
  if (dn->type == DEV_TTY) {
    if ((events & 0x001) &&
#ifdef __x86_64__
        serial_received()
#else
        kbd_has_char()
#endif
    )
      revents |= 0x001;
    if (events & 0x004)
      revents |= 0x004;
  } else {
    if (events & 0x001)
      revents |= 0x001;
    if (events & 0x004)
      revents |= 0x004;
  }
  return revents;
}

static int devfs_readdir(struct vnode *dir, uint32_t index, struct vnode **out) {
  if (!dir || !out)
    return 0;
  if (dir == &dev_root.vnode) {
    switch (index) {
    case 0:
      *out = &dev_tty.vnode;
      return 1;
    case 1:
      *out = &dev_null.vnode;
      return 1;
    case 2:
      *out = &dev_zero.vnode;
      return 1;
    case 3:
      *out = &dev_fb0.vnode;
      return 1;
    case 4:
      *out = &dev_dri.vnode;
      return 1;
    case 5:
      *out = &dev_input.vnode;
      return 1;
    case 6:
      *out = &dev_console.vnode;
      return 1;
    default:
      *out = NULL;
      return 0;
    }
  }
  if (dir == &dev_dri.vnode) {
    if (index == 0) {
      *out = &dev_card0.vnode;
      return 1;
    }
    *out = NULL;
    return 0;
  }
  if (dir == &dev_input.vnode) {
    switch (index) {
    case 0:
      *out = &dev_event0.vnode;
      return 1;
    case 1:
      *out = &dev_event1.vnode;
      return 1;
    default:
      *out = NULL;
      return 0;
    }
  }
  return 0;
}

static void devfs_init_node(dev_node_t *node, const char *name,
                            vnode_type_t type, dev_type_t dev_type,
                            vnode_t *parent) {
  memset(node, 0, sizeof(*node));
  node->name = name;
  node->type = dev_type;
  node->vnode.name = name;
  node->vnode.type = type;
  node->vnode.ops = &devfs_ops;
  node->vnode.internal = node;
  node->vnode.parent = parent;
  node->vnode.size = 0;
}

vnode_t *devfs_root(void) {
  static int inited = 0;
  if (inited)
    return &dev_root.vnode;
  inited = 1;

  devfs_ops.open = devfs_open;
  devfs_ops.read = devfs_read;
  devfs_ops.write = devfs_write;
  devfs_ops.close = devfs_close;
  devfs_ops.poll = devfs_poll;
  devfs_ops.ioctl = devfs_ioctl;
  devfs_ops.readdir = devfs_readdir;

  devfs_init_node(&dev_root, "dev", VNODE_DIR, DEV_NULL, NULL);
  dev_root.vnode.parent = NULL;

  devfs_init_node(&dev_tty, "tty", VNODE_DEV, DEV_TTY, &dev_root.vnode);
  devfs_init_node(&dev_null, "null", VNODE_DEV, DEV_NULL, &dev_root.vnode);
  devfs_init_node(&dev_zero, "zero", VNODE_DEV, DEV_ZERO, &dev_root.vnode);
  devfs_init_node(&dev_fb0, "fb0", VNODE_DEV, DEV_FB0, &dev_root.vnode);
  devfs_init_node(&dev_dri, "dri", VNODE_DIR, DEV_DRI_DIR, &dev_root.vnode);
  devfs_init_node(&dev_card0, "card0", VNODE_DEV, DEV_DRI_CARD0,
                  &dev_dri.vnode);
  devfs_init_node(&dev_input, "input", VNODE_DIR, DEV_INPUT_DIR,
                  &dev_root.vnode);
  devfs_init_node(&dev_console, "console", VNODE_DEV, DEV_CONSOLE,
                  &dev_root.vnode);
  devfs_init_node(&dev_event0, "event0", VNODE_DEV, DEV_INPUT_EVENT0,
                  &dev_input.vnode);
  devfs_init_node(&dev_event1, "event1", VNODE_DEV, DEV_INPUT_EVENT1,
                  &dev_input.vnode);

  return &dev_root.vnode;
}
