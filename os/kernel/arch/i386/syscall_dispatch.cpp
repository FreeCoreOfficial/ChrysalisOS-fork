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
#include "../../ui/flyui/draw.h"
#include "../../ui/wm/wm.h"
#include <stdint.h>

extern "C" void schedule();
extern "C" void serial(const char *fmt, ...);

#ifdef __cplusplus
extern "C" {
#endif

/* === helper intern === */
static int sys_write(int fd, const char *s, uint32_t size) {
  if (!s)
    return -1;

  /* FD 1 or 2: Terminal */
  if (fd == 1 || fd == 2) {
    for (uint32_t i = 0; i < size; i++) {
      terminal_putchar(s[i]);
    }
    return size;
  }

  /* Other FDs: VFS File */
  pcb_t *cur = pcb_get_current();
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
  vnode_t *node = vfs_resolve(path);
  if (!node)
    return -1;

  if (node->ops && node->ops->open) {
    if (node->ops->open(node) < 0)
      return -1;
  }

  pcb_t *cur = pcb_get_current();
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
  pcb_t *cur = pcb_get_current();
  if (!cur || fd < 0 || fd >= MAX_FILES_PER_PROCESS)
    return -1;
  file_t *f = cur->files[fd];
  if (!f || !f->node || !f->node->ops || !f->node->ops->read)
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

  kfree(f);
  cur->files[fd] = NULL;
  return 0;
}

int syscall_dispatch(uint32_t num, uint32_t a1, uint32_t a2, uint32_t a3,
                     uint32_t a4, uint32_t a5, uint32_t a6) {
  // serial("[SYSCALL] %d (a1=%x a2=%x a3=%x a4=%x a5=%x a6=%x)\n", num, a1, a2,
  //        a3, a4, a5, a6);
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

  case SYS_GET_EVENT: {
    input_event_t *out_ev = (input_event_t *)(uintptr_t)a1;
    pcb_t *cur = pcb_get_current();

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
    if (win && win->surface && path) {
      /* Use internal BMP loader to draw directly to window surface */
      /* Note: We need a way to draw at X,Y offset? fly_load_bmp_to_surface
         loads to the *whole* surface generally, or we need a helper to load
         to a temp surface and blit.

         Existing fly_load_bmp_to_surface(surface, path) overwrites surface
         content. If we want to position it, we need a blit.

         For now, for image-viewer, we will just load it to the window surface
         directly. Ideally: sys_draw_bmp(win, path, x, y). But we don't have a
         direct "load file to X,Y of surface" helper exposed easily yet.

         Let's assume we want to load full image to 0,0 or stretch?
         Actually, let's look at `fly_load_bmp_to_surface` again. It iterates
         through file and writes to surface pixels. It does bound checking. If
         we want to support offset, we should probably add `x, y` args to
         `fly_load_bmp_to_surface` or creating a new helper.

         Alternative: `p_load_bmp` returns a handle (surface ID) and we have
         `p_draw_surface`. But we only have `SYS_FLY_DRAW_BMP` slot for now.

         Let's invoke `fly_load_bmp_to_surface` for now. It draws at 0,0.
      */
      extern int fly_load_bmp_to_surface(surface_t * surf, const char *path);
      return fly_load_bmp_to_surface(win->surface, path);
    }
    return -1;
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
    return cmd_exec_capture(line, out, out_cap);
  }

  default:
    terminal_printf("[syscall] invalid syscall %d\n", num);
    return -1;
  }
}

#ifdef __cplusplus
}
#endif
