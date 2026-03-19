#include "procfs.h"
#include "../vfs/fs_ops.h"
#include "../../mem/kmalloc.h"
#include "../../string.h"
#ifndef __x86_64__
#include "../../time/timer.h"
#endif
#include "../../time/clock.h"
#include "../../memory/pmm.h"
#ifndef __x86_64__
#include "../../include/stdio.h"
#else
#include <stdarg.h>
#endif

#ifdef __x86_64__
static int procfs_snprintf(char *out, size_t cap, const char *fmt, ...) {
  if (!out || cap == 0 || !fmt)
    return 0;
  va_list ap;
  va_start(ap, fmt);
  size_t pos = 0;
  for (const char *p = fmt; *p && pos + 1 < cap; p++) {
    if (*p != '%') {
      out[pos++] = *p;
      continue;
    }
    p++;
    if (*p == 'u') {
      unsigned int v = va_arg(ap, unsigned int);
      char tmp[16];
      int i = 0;
      if (v == 0)
        tmp[i++] = '0';
      else {
        while (v && i < 15) {
          tmp[i++] = (char)('0' + (v % 10));
          v /= 10;
        }
      }
      while (i-- > 0 && pos + 1 < cap)
        out[pos++] = tmp[i];
      continue;
    }
    if (*p == 's') {
      const char *s = va_arg(ap, const char *);
      if (!s)
        s = "";
      while (*s && pos + 1 < cap)
        out[pos++] = *s++;
      continue;
    }
    if (*p == '0' && p[1] == '2' && p[2] == 'u') {
      unsigned int v = va_arg(ap, unsigned int);
      unsigned int tens = (v / 10) % 10;
      unsigned int ones = v % 10;
      if (pos + 1 < cap)
        out[pos++] = (char)('0' + tens);
      if (pos + 1 < cap)
        out[pos++] = (char)('0' + ones);
      p += 2;
      continue;
    }
  }
  out[pos] = 0;
  va_end(ap);
  return (int)pos;
}
#define snprintf procfs_snprintf
#endif

#ifdef __x86_64__
extern uint32_t g_total_ram_mb;
#endif

typedef enum {
  PROC_UPTIME = 1,
  PROC_MEMINFO,
  PROC_VERSION,
  PROC_CPUINFO,
  PROC_CMDLINE
} proc_type_t;

typedef struct proc_node {
  vnode_t vnode;
  const char *name;
  proc_type_t type;
} proc_node_t;

static fs_ops_t procfs_ops;
static proc_node_t proc_root;
static proc_node_t proc_uptime;
static proc_node_t proc_meminfo;
static proc_node_t proc_version;
static proc_node_t proc_cpuinfo;
static proc_node_t proc_cmdline;

static int procfs_open(struct vnode *n) {
  (void)n;
  return 0;
}

static int procfs_close(struct vnode *n) {
  (void)n;
  return 0;
}

static int procfs_format(proc_node_t *pn, char *out, size_t cap) {
  if (!pn || !out || cap == 0)
    return 0;
  out[0] = 0;
  switch (pn->type) {
  case PROC_UPTIME: {
#ifdef __x86_64__
    uint32_t ms = 0;
#else
    uint32_t ms = timer_uptime_ms();
#endif
    uint32_t sec = ms / 1000;
    uint32_t frac = (ms % 1000) / 10;
    return snprintf(out, cap, "%u.%02u\n", sec, frac);
  }
  case PROC_MEMINFO: {
    uint32_t total_kb = 0;
    uint32_t free_kb = 0;
#ifdef __x86_64__
    total_kb = g_total_ram_mb * 1024;
    free_kb = total_kb / 2;
#else
    uint32_t total_frames = pmm_total_frames();
    uint32_t used_frames = pmm_used_frames();
    uint32_t free_frames =
        (total_frames > used_frames) ? (total_frames - used_frames) : 0;
    total_kb = (total_frames * 4096) / 1024;
    free_kb = (free_frames * 4096) / 1024;
#endif
    return snprintf(out, cap, "MemTotal: %u kB\nMemFree: %u kB\n", total_kb,
                    free_kb);
  }
  case PROC_VERSION:
#ifdef CHRYVER
    return snprintf(out, cap, "ChrysalisOS %s\n", CHRYVER);
#else
    return snprintf(out, cap, "ChrysalisOS unknown\n");
#endif
  case PROC_CPUINFO:
    return snprintf(out, cap,
                    "processor\t: 0\nvendor_id\t: chrysalis\nmodel name\t: "
                    "ChrysalisOS CPU\n");
  case PROC_CMDLINE:
    return snprintf(out, cap, "\n");
  default:
    return 0;
  }
}

static int procfs_read(struct vnode *n, uint32_t off, uint8_t *buf,
                       uint32_t size) {
  if (!n || !buf || size == 0)
    return 0;
  proc_node_t *pn = (proc_node_t *)n;
  char tmp[512];
  int len = procfs_format(pn, tmp, sizeof(tmp));
  if (len <= 0)
    return 0;
  if (off >= (uint32_t)len)
    return 0;
  uint32_t to_copy = size;
  if (off + to_copy > (uint32_t)len)
    to_copy = (uint32_t)len - off;
  memcpy(buf, tmp + off, to_copy);
  return (int)to_copy;
}

static uint32_t procfs_poll(struct vnode *n, uint32_t events) {
  if (!n)
    return 0;
  uint32_t revents = 0;
  if (events & 0x001)
    revents |= 0x001;
  if (events & 0x004)
    revents |= 0x004;
  return revents;
}

static int procfs_readdir(struct vnode *dir, uint32_t index,
                          struct vnode **out) {
  if (!dir || !out)
    return 0;
  if (dir != &proc_root.vnode)
    return 0;
  switch (index) {
  case 0:
    *out = &proc_uptime.vnode;
    return 1;
  case 1:
    *out = &proc_meminfo.vnode;
    return 1;
  case 2:
    *out = &proc_version.vnode;
    return 1;
  case 3:
    *out = &proc_cpuinfo.vnode;
    return 1;
  case 4:
    *out = &proc_cmdline.vnode;
    return 1;
  default:
    *out = NULL;
    return 0;
  }
}

static void procfs_init_node(proc_node_t *node, const char *name,
                             vnode_type_t type, proc_type_t proc_type,
                             vnode_t *parent) {
  memset(node, 0, sizeof(*node));
  node->name = name;
  node->type = proc_type;
  node->vnode.name = name;
  node->vnode.type = type;
  node->vnode.ops = &procfs_ops;
  node->vnode.internal = node;
  node->vnode.parent = parent;
}

vnode_t *procfs_root(void) {
  static int inited = 0;
  if (inited)
    return &proc_root.vnode;
  inited = 1;

  procfs_ops.open = procfs_open;
  procfs_ops.read = procfs_read;
  procfs_ops.write = NULL;
  procfs_ops.close = procfs_close;
  procfs_ops.poll = procfs_poll;
  procfs_ops.readdir = procfs_readdir;

  procfs_init_node(&proc_root, "proc", VNODE_DIR, PROC_CMDLINE, NULL);
  proc_root.vnode.parent = NULL;

  procfs_init_node(&proc_uptime, "uptime", VNODE_FILE, PROC_UPTIME,
                   &proc_root.vnode);
  procfs_init_node(&proc_meminfo, "meminfo", VNODE_FILE, PROC_MEMINFO,
                   &proc_root.vnode);
  procfs_init_node(&proc_version, "version", VNODE_FILE, PROC_VERSION,
                   &proc_root.vnode);
  procfs_init_node(&proc_cpuinfo, "cpuinfo", VNODE_FILE, PROC_CPUINFO,
                   &proc_root.vnode);
  procfs_init_node(&proc_cmdline, "cmdline", VNODE_FILE, PROC_CMDLINE,
                   &proc_root.vnode);

  return &proc_root.vnode;
}
