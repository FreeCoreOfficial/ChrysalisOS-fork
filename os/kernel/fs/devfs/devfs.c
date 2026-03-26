#include "devfs.h"
#include "../vfs/fs_ops.h"
#include "../../mem/kmalloc.h"
#include "../../string.h"
#include "../../video/kms.h"
#ifndef __x86_64__
#include "../../terminal.h"
#include "../../input/keyboard_buffer.h"
#include "../../video/framebuffer.h"
#include "../../time/timer.h"
#endif
#include "../../drivers/serial.h"
#include "../../video/gpu.h"
#include "../../mem/user64_vm.h"
#include "../../vt/vt.h"

static fs_ops_t devfs_ops;
static dev_node_t dev_root;
static dev_node_t dev_tty;
static dev_node_t dev_tty0;
static dev_node_t dev_null;
static dev_node_t dev_zero;
static dev_node_t dev_fb0;
static dev_node_t dev_dri;
static dev_node_t dev_card0;
static dev_node_t dev_input;
static dev_node_t dev_event0;
static dev_node_t dev_event1;
static dev_node_t dev_console;

#define FBIOGET_VSCREENINFO 0x4600
#define FBIOGET_FSCREENINFO 0x4602

/* VT/KD ioctls (minimal subset for Xorg fbdev) */
#define KDSETMODE 0x4B3A
#define KDGETMODE 0x4B3B
#define KDSKBMODE 0x4B45
#define KDGKBMODE 0x4B44
#define KD_TEXT 0x00
#define KD_GRAPHICS 0x01
#define K_XLATE 0x01
#define K_RAW 0x00

static int g_kd_mode = KD_TEXT;
static int g_kbd_mode = K_XLATE;

#define VT_GETSTATE 0x5603
#define VT_ACTIVATE 0x5606
#define VT_WAITACTIVE 0x5607
#define VT_SETMODE 0x5602

struct vt_stat {
  uint16_t v_active;
  uint16_t v_signal;
  uint16_t v_state;
};

struct vt_mode {
  uint8_t mode;
  uint8_t waitv;
  uint16_t relsig;
  uint16_t acqsig;
  uint16_t frsig;
};

struct fb_bitfield {
  uint32_t offset;
  uint32_t length;
  uint32_t msb_right;
};

struct fb_var_screeninfo {
  uint32_t xres;
  uint32_t yres;
  uint32_t xres_virtual;
  uint32_t yres_virtual;
  uint32_t xoffset;
  uint32_t yoffset;
  uint32_t bits_per_pixel;
  uint32_t grayscale;
  struct fb_bitfield red;
  struct fb_bitfield green;
  struct fb_bitfield blue;
  struct fb_bitfield transp;
  uint32_t nonstd;
  uint32_t activate;
  uint32_t height;
  uint32_t width;
  uint32_t accel_flags;
  uint32_t pixclock;
  uint32_t left_margin;
  uint32_t right_margin;
  uint32_t upper_margin;
  uint32_t lower_margin;
  uint32_t hsync_len;
  uint32_t vsync_len;
  uint32_t sync;
  uint32_t vmode;
  uint32_t rotate;
  uint32_t colorspace;
  uint32_t reserved[4];
};

struct fb_fix_screeninfo {
  char id[16];
  uint64_t smem_start;
  uint32_t smem_len;
  uint32_t type;
  uint32_t type_aux;
  uint32_t visual;
  uint16_t xpanstep;
  uint16_t ypanstep;
  uint16_t ywrapstep;
  uint32_t line_length;
  uint32_t mmio_start;
  uint32_t mmio_len;
  uint32_t accel;
  uint16_t capabilities;
  uint16_t reserved[2];
};

#include "../../input/input.h"
#include "../../linux_compat/linux_abi.h"
#ifdef __x86_64__
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
  if (dn->type == DEV_TTY || dn->type == DEV_CONSOLE) {
    switch (cmd) {
    case KDSETMODE:
      if (!arg)
        return -1;
      g_kd_mode = *(int *)arg;
      return 0;
    case KDGETMODE:
      if (!arg)
        return -1;
      *(int *)arg = g_kd_mode;
      return 0;
    case KDSKBMODE:
      if (!arg)
        return -1;
      g_kbd_mode = *(int *)arg;
      return 0;
    case KDGKBMODE:
      if (!arg)
        return -1;
      *(int *)arg = g_kbd_mode;
      return 0;
    case VT_GETSTATE: {
      if (!arg)
        return -1;
      struct vt_stat st;
      memset(&st, 0, sizeof(st));
      st.v_active = (uint16_t)(vt_active() + 1);
      st.v_state = (uint16_t)(1u << vt_active());
      return copy_to_user(arg, &st, sizeof(st));
    }
    case VT_ACTIVATE:
    case VT_WAITACTIVE: {
      int vt = (int)(uintptr_t)arg;
      if (vt <= 0)
        return -1;
      vt_switch(vt - 1);
      return 0;
    }
    case VT_SETMODE:
      return 0;
    default:
      return -1;
    }
  }
  if (dn->type == DEV_FB0) {
    gpu_device_t *gpu = gpu_get_primary();
    if (!gpu || !arg)
      return -1;
    if (cmd == FBIOGET_VSCREENINFO) {
      struct fb_var_screeninfo info;
      memset(&info, 0, sizeof(info));
      info.xres = gpu->width;
      info.yres = gpu->height;
      info.xres_virtual = gpu->width;
      info.yres_virtual = gpu->height;
      info.bits_per_pixel = gpu->bpp;
      info.red.offset = 16;
      info.red.length = 8;
      info.green.offset = 8;
      info.green.length = 8;
      info.blue.offset = 0;
      info.blue.length = 8;
      info.transp.offset = 24;
      info.transp.length = 8;
      return copy_to_user(arg, &info, sizeof(info));
    }
    if (cmd == FBIOGET_FSCREENINFO) {
      struct fb_fix_screeninfo info;
      memset(&info, 0, sizeof(info));
      memcpy(info.id, "chrysfb0", 9);
      info.smem_start = (uint64_t)gpu->phys_addr;
      info.smem_len = gpu->pitch * gpu->height;
      info.type = 0;   /* FB_TYPE_PACKED_PIXELS */
      info.visual = 2; /* FB_VISUAL_TRUECOLOR */
      info.line_length = gpu->pitch;
      return copy_to_user(arg, &info, sizeof(info));
    }
    return -1;
  }
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
#ifdef __x86_64__
      while (!serial_received()) {
        for (volatile int i = 0; i < 100000; i++)
          asm volatile("");
      }
      char c = serial_read();
#else
      while (!kbd_has_char()) {
        sleep(1);
      }
      char c = kbd_get_char();
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
    uint32_t w = 0, h = 0, pitch = 0;
    uint8_t *fb = NULL;
#ifdef __x86_64__
    gpu_device_t *gpu = gpu_get_primary();
    if (!gpu || !gpu->virt_addr || gpu->pitch == 0 || gpu->height == 0)
      return 0;
    fb = (uint8_t *)gpu->virt_addr;
    w = gpu->width;
    h = gpu->height;
    pitch = gpu->pitch;
#else
    uint8_t bpp = 0;
    fb_get_info(&w, &h, &pitch, &bpp, &fb);
    if (!fb || pitch == 0 || h == 0)
      return 0;
#endif
    uint32_t fb_size = pitch * h;
    if (off >= fb_size)
      return 0;
    uint32_t to_copy = size;
    if (off + to_copy > fb_size)
      to_copy = fb_size - off;
    memcpy(buf, fb + off, to_copy);
    return (int)to_copy;
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
  case DEV_CONSOLE:
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
    uint32_t w = 0, h = 0, pitch = 0;
    uint8_t *fb = NULL;
#ifdef __x86_64__
    gpu_device_t *gpu = gpu_get_primary();
    if (!gpu || !gpu->virt_addr || gpu->pitch == 0 || gpu->height == 0)
      return 0;
    fb = (uint8_t *)gpu->virt_addr;
    w = gpu->width;
    h = gpu->height;
    pitch = gpu->pitch;
#else
    uint8_t bpp = 0;
    fb_get_info(&w, &h, &pitch, &bpp, &fb);
    if (!fb || pitch == 0 || h == 0)
      return 0;
#endif
    uint32_t fb_size = pitch * h;
    if (off >= fb_size)
      return 0;
    uint32_t to_copy = size;
    if (off + to_copy > fb_size)
      to_copy = fb_size - off;
    memcpy(fb + off, buf, to_copy);
    return (int)to_copy;
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
  if (dn->type == DEV_TTY || dn->type == DEV_CONSOLE) {
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
      *out = &dev_tty0.vnode;
      return 1;
    case 2:
      *out = &dev_null.vnode;
      return 1;
    case 3:
      *out = &dev_zero.vnode;
      return 1;
    case 4:
      *out = &dev_fb0.vnode;
      return 1;
    case 5:
      *out = &dev_dri.vnode;
      return 1;
    case 6:
      *out = &dev_input.vnode;
      return 1;
    case 7:
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
  devfs_init_node(&dev_tty0, "tty0", VNODE_DEV, DEV_TTY, &dev_root.vnode);
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
