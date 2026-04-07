/* kernel/arch/i386/syscall_dispatch.c */
#include "../../fs/vfs/fs_ops.h"
#include "../../fs/vfs/vfs.h"
#include "../../include/chrysalis/syscall_nums.h"
#include "../../input/input.h"
#include "../../mem/kmalloc.h"
#include "../../cmds/command_exec.h"
#include "../../sched/pcb.h"
#include "../../terminal.h"
#include "../../time/clock.h"
#include "../../time/timer.h"
#include "../../toolchain/cc.h"
#include "../../ui/flyui/bmp.h"
#include "../../ui/flyui/draw.h"
#include "../../ui/wm/wm.h"
#include "../../user/user.h"
#include "../../mm/paging.h"
#include <stdint.h>

extern "C" void schedule();
extern "C" void serial(const char *fmt, ...);

#ifdef __cplusplus
extern "C" {
#endif

static bool range_ok_pd(uint32_t *pd, uint32_t start, uint32_t end,
                        bool require_user) {
  if (!pd)
    return false;
  uint32_t addr = start & PAGE_FRAME_MASK;
  while (addr <= end) {
    uint32_t *pte = get_pte_for(pd, addr, 0);
    if (!pte || !(*pte & PAGE_PRESENT))
      return false;
    if (require_user && !(*pte & PAGE_USER))
      return false;
    if (addr + 0x1000 == 0)
      break;
    addr += 0x1000;
  }
  return true;
}

static bool syscall_range_ok(pcb_t *cur, const void *ptr, uint32_t len) {
  if (!ptr || len == 0)
    return false;
  uint32_t start = (uint32_t)(uintptr_t)ptr;
  uint32_t end = start + len - 1;
  if (end < start)
    return false;

  if (start < KERNEL_BASE && end < KERNEL_BASE) {
    uint32_t *pd = kernel_page_directory;
    if (cur && cur->cr3) {
      pd = (uint32_t *)(uintptr_t)(cur->cr3 + KERNEL_BASE);
    }
    return range_ok_pd(pd, start, end, true);
  }

  if (start >= KERNEL_BASE && end >= KERNEL_BASE) {
    return range_ok_pd(kernel_page_directory, start, end, false);
  }

  return false;
}

extern "C" int syscall_user_range_ok(const void *ptr, uint32_t len) {
  if (!ptr || len == 0)
    return 0;
  uint32_t start = (uint32_t)(uintptr_t)ptr;
  uint32_t end = start + len - 1;
  if (end < start)
    return 0;
  if (start >= KERNEL_BASE || end >= KERNEL_BASE)
    return 0;
  pcb_t *cur = pcb_get_current();
  if (!cur)
    return 0;
  uint32_t *pd = kernel_page_directory;
  if (cur->cr3) {
    pd = (uint32_t *)(uintptr_t)(cur->cr3 + KERNEL_BASE);
  }
  return range_ok_pd(pd, start, end, true) ? 1 : 0;
}

/* === helper intern === */
static int sys_write(int fd, const char *s, uint32_t size) {
  if (!s)
    return -1;
  if (size == 0)
    return 0;

  pcb_t *cur = pcb_get_current();
  if (!syscall_range_ok(cur, s, size))
    return -1;

  /* FD 1 or 2: Terminal */
  if (fd == 1 || fd == 2) {
    for (uint32_t i = 0; i < size; i++) {
      terminal_putchar(s[i]);
    }
    return size;
  }

  /* Other FDs: VFS File */
  if (!cur || fd < 0 || fd >= MAX_FILES_PER_PROCESS)
    return -1;

  file_t *f = cur->files[fd];
  if (!f || !f->node || !f->node->ops || !f->node->ops->write)
    return -1;

  int bytes = f->node->ops->write(f->node, f->offset, (const uint8_t *)s, size);
  if (bytes > 0) {
    f->offset += bytes;
  }
  return bytes;
}

static int sys_open(const char *path, int flags) {
  if (!path)
    return -1;

  pcb_t *cur = pcb_get_current();
  auto user_strnlen = [&](const char *s, uint32_t max) -> int {
    for (uint32_t i = 0; i < max; i++) {
      if (!syscall_range_ok(cur, (const void *)(uintptr_t)(s + i), 1))
        return -1;
      if (s[i] == 0)
        return (int)i;
    }
    return -1;
  };

  if (user_strnlen(path, 256) < 0)
    return -1;

  vnode_t *node = vfs_resolve(path);
  if (!node)
    return -1;

  if (node->ops && node->ops->open) {
    if (node->ops->open(node) < 0)
      return -1;
  }

  if (!cur)
    return -1;

  for (int i = 0; i < MAX_FILES_PER_PROCESS; i++) {
    if (cur->files[i] == NULL) {
      file_t *f = (file_t *)kmalloc(sizeof(file_t));
      f->node = node;
      f->offset = 0;
      f->flags = flags;
      cur->files[i] = f;
      return i;
    }
  }
  return -1;
}

static int sys_read(int fd, void *buf, uint32_t size) {
  if (!buf || size == 0)
    return -1;
  pcb_t *cur = pcb_get_current();
  if (!cur || fd < 0 || fd >= MAX_FILES_PER_PROCESS)
    return -1;
  file_t *f = cur->files[fd];
  if (!f || !f->node || !f->node->ops || !f->node->ops->read)
    return -1;

  if (!syscall_range_ok(cur, buf, size))
    return -1;

  int bytes = f->node->ops->read(f->node, f->offset, (uint8_t *)buf, size);
  if (bytes > 0) {
    f->offset += bytes;
  }
  return bytes;
}

static int sys_close(int fd) {
  pcb_t *cur = pcb_get_current();
  if (!cur || fd < 0 || fd >= MAX_FILES_PER_PROCESS)
    return -1;
  file_t *f = cur->files[fd];
  if (!f)
    return -1;

  if (f->node && f->node->ops && f->node->ops->close)
    f->node->ops->close(f->node);
  kfree(f);
  cur->files[fd] = NULL;
  return 0;
}

static int sys_ioctl(int fd, uint32_t cmd, void *arg) {
  pcb_t *cur = pcb_get_current();
  if (!cur || fd < 0 || fd >= MAX_FILES_PER_PROCESS)
    return -1;
  file_t *f = cur->files[fd];
  if (!f || !f->node || !f->node->ops || !f->node->ops->ioctl)
    return -1;
  return f->node->ops->ioctl(f->node, cmd, arg);
}

int syscall_dispatch_chrys(uint32_t num, uint32_t a1, uint32_t a2,
                           uint32_t a3, uint32_t a4, uint32_t a5,
                           uint32_t a6) {
  switch (num) {
  case SYS_WRITE:
    return sys_write((int)a1, (const char *)(uintptr_t)a2, a3);

  case SYS_READ:
    return sys_read((int)a1, (void *)(uintptr_t)a2, a3);

  case SYS_OPEN:
    return sys_open((const char *)(uintptr_t)a1, (int)a2);

  case SYS_COMPILE_AND_RUN:
    return toolchain_compile_and_run((const char *)(uintptr_t)a1);

  case SYS_CLOSE:
    return sys_close((int)a1);

  case SYS_IOCTL:
    return sys_ioctl((int)a1, a2, (void *)(uintptr_t)a3);

  case SYS_EXIT:
    terminal_printf("[syscall] process exit code=%d\n", a1);
    task_exit((int)a1);
    return 0;

  case SYS_WM_CREATE_WINDOW: {
    surface_t *s = surface_create(a1, a2);
    if (!s)
      return 0;
    window_t *win = wm_create_window(s, a3, a4);
    if (win && a5) {
      wm_set_title(win, (const char *)(uintptr_t)a5);
    }
    /* Link window to task for event routing */
    pcb_t *cur = pcb_get_current();
    if (cur && win) {
      win->owner = cur;
    }
    return (uint32_t)(uintptr_t)win;
  }

  case SYS_WM_DESTROY_WINDOW: {
    window_t *win = (window_t *)(uintptr_t)a1;
    wm_destroy_window(win);
    return 0;
  }

  case SYS_WM_MARK_DIRTY:
    wm_mark_dirty();
    wm_render();
    return 0;

  case SYS_WM_GET_POS: {
    window_t *win = (window_t *)(uintptr_t)a1;
    if (win)
      return (uint32_t)((win->x << 16) | (win->y & 0xFFFF));
    return 0;
  }

  case SYS_WM_GET_SIZE: {
    window_t *win = (window_t *)(uintptr_t)a1;
    if (win)
      return (uint32_t)(((win->w & 0xFFFF) << 16) | (win->h & 0xFFFF));
    return 0;
  }

  case SYS_GET_EVENT: {
    input_event_t *out_ev = (input_event_t *)(uintptr_t)a1;
    pcb_t *cur = pcb_get_current();

    if (!syscall_range_ok(cur, out_ev, sizeof(input_event_t)))
      return -1;

    /*
     * Standalone apps should ONLY receive events pushed to their specific
     * task queue by the Window Manager. Stealing from the global input_pop
     * starves the main GUI loop and causes freezes.
     */
    if (cur) {
      if (task_pop_event(cur, out_ev))
        return 1;
    }

    /* If no event, yield to allow others to run */
    yield();
    return 0;
  }

  case SYS_SLEEP:
    sleep((uint32_t)a1);
    return 0;

  case SYS_YIELD:
    schedule();
    return 0;

  case SYS_FLY_DRAW_TEXT: {
    window_t *win = (window_t *)(uintptr_t)a1;
    if (win && win->surface) {
      fly_draw_text(win->surface, a2, a3, (const char *)(uintptr_t)a4, a5);
    }
    return 0;
  }

  case SYS_FLY_DRAW_RECT_FILL: {
    window_t *win = (window_t *)(uintptr_t)a1;
    if (win && win->surface) {
      fly_draw_rect_fill(win->surface, a2, a3, a4, (uint16_t)a5, a6);
    }
    return 0;
  }

  case SYS_FLY_DRAW_BMP: {
    window_t *win = (window_t *)(uintptr_t)a1;
    const char *path = (const char *)(uintptr_t)a2;
    if (win && win->surface && path)
      return fly_load_bmp_to_surface(win->surface, path);
    return -1;
  }

  case SYS_FLY_DRAW_BMP_FIT: {
    window_t *win = (window_t *)(uintptr_t)a1;
    const char *path = (const char *)(uintptr_t)a2;
    if (!win || !win->surface || !path)
      return -1;

    surface_t *img = fly_load_bmp(path);
    if (!img)
      return -1;

    int dst_w = (int)win->surface->width;
    int dst_h = (int)win->surface->height;
    if (dst_w <= 0 || dst_h <= 0 || img->width == 0 || img->height == 0) {
      surface_destroy(img);
      return -1;
    }

    fly_draw_rect_fill(win->surface, 0, 0, dst_w, dst_h, 0xFF101010);

    int draw_w = dst_w;
    int draw_h = (int)(((uint64_t)img->height * (uint64_t)dst_w) /
                       (uint64_t)img->width);
    if (draw_h > dst_h) {
      draw_h = dst_h;
      draw_w = (int)(((uint64_t)img->width * (uint64_t)dst_h) /
                     (uint64_t)img->height);
    }
    if (draw_w < 1)
      draw_w = 1;
    if (draw_h < 1)
      draw_h = 1;

    int draw_x = (dst_w - draw_w) / 2;
    int draw_y = (dst_h - draw_h) / 2;
    fly_draw_surface_scaled(win->surface, img, draw_x, draw_y, draw_w, draw_h);

    surface_destroy(img);
    return 0;
  }

  case SYS_GET_TIME: {
    datetime t;
    time_get_local(&t);
    /* Pack time into 32-bit: HH:MM:SS */
    return (uint32_t)((t.hour << 16) | (t.minute << 8) | t.second);
  }

  case SYS_GET_LAUNCH_ARG: {
    char *user_buf = (char *)(uintptr_t)a1;
    uint32_t buf_size = a2;
    pcb_t *cur = pcb_get_current();
    if (!user_buf || buf_size == 0 || !cur)
      return -1;
    if (!syscall_range_ok(cur, user_buf, buf_size))
      return -1;

    uint32_t i = 0;
    while (i + 1 < buf_size && cur->launch_arg[i]) {
      user_buf[i] = cur->launch_arg[i];
      i++;
    }
    user_buf[i] = 0;
    return (int)i;
  }

  case SYS_CMD_EXEC_CAPTURE: {
    const char *line = (const char *)(uintptr_t)a1;
    char *out = (char *)(uintptr_t)a2;
    uint32_t out_cap = a3;
    pcb_t *cur = pcb_get_current();
    auto user_strnlen = [&](const char *s, uint32_t max) -> int {
      for (uint32_t i = 0; i < max; i++) {
        if (!syscall_range_ok(cur, (const void *)(uintptr_t)(s + i), 1))
          return -1;
        if (s[i] == 0)
          return (int)i;
      }
      return -1;
    };
    if (!line || !out || out_cap == 0)
      return -1;
    if (user_strnlen(line, 512) < 0)
      return -1;
    if (!syscall_range_ok(cur, out, out_cap))
      return -1;
    return cmd_exec_capture(line, out, out_cap);
  }

  case SYS_USER_IS_LOGGED:
    return user_get_current() ? 1 : 0;

  default:
    terminal_printf("[syscall] invalid syscall %d\n", num);
    return -1;
  }
}

int syscall_dispatch(uint32_t num, uint32_t a1, uint32_t a2, uint32_t a3,
                     uint32_t a4, uint32_t a5, uint32_t a6) {
  pcb_t *cur = pcb_get_current();
  if (cur) {
    cur->last_syscall = num;
    cur->last_syscall_a1 = a1;
    cur->last_syscall_a2 = a2;
    cur->last_syscall_a3 = a3;
  }

  return syscall_dispatch_chrys(num, a1, a2, a3, a4, a5, a6);
}

#ifdef __cplusplus
}
#endif
