/* user/libpetal/src/libpetal.c */
#include "../include/petal.h"

static size_t strlen(const char *s) {
  size_t len = 0;
  while (s[len])
    len++;
  return len;
}

static inline int syscall0(int num) {
  int ret;
  asm volatile("int $0x80" : "=a"(ret) : "a"(num));
  return ret;
}

static inline int syscall1(int num, uint32_t a1) {
  int ret;
  asm volatile("int $0x80" : "=a"(ret) : "a"(num), "b"(a1));
  return ret;
}

static inline int syscall2(int num, uint32_t a1, uint32_t a2) {
  int ret;
  asm volatile("int $0x80" : "=a"(ret) : "a"(num), "b"(a1), "c"(a2));
  return ret;
}

static inline int syscall3(int num, uint32_t a1, uint32_t a2, uint32_t a3) {
  int ret;
  asm volatile("int $0x80" : "=a"(ret) : "a"(num), "b"(a1), "c"(a2), "d"(a3));
  return ret;
}

static inline int syscall5(int num, uint32_t a1, uint32_t a2, uint32_t a3,
                           uint32_t a4, uint32_t a5) {
  int ret;
  asm volatile("int $0x80"
               : "=a"(ret)
               : "a"(num), "b"(a1), "c"(a2), "d"(a3), "S"(a4), "D"(a5));
  return ret;
}

static inline int syscall6(int num, uint32_t a1, uint32_t a2, uint32_t a3,
                           uint32_t a4, uint32_t a5, uint32_t a6) {
  int ret;
  asm volatile("pushl %%ebp\n"
               "movl %7, %%ebp\n"
               "int $0x80\n"
               "popl %%ebp"
               : "=a"(ret)
               : "a"(num), "b"(a1), "c"(a2), "d"(a3), "S"(a4), "D"(a5), "m"(a6)
               : "memory");
  return ret;
}

void p_exit(int code) { syscall1(SYS_EXIT, (uint32_t)code); }

void p_write(const char *s) {
  syscall3(SYS_WRITE, 1, (uint32_t)(uintptr_t)s, (uint32_t)strlen(s));
}

void *p_wm_create_window(int w, int h, int x, int y, const char *title) {
  return (void *)(uintptr_t)syscall5(SYS_WM_CREATE_WINDOW, (uint32_t)w,
                                     (uint32_t)h, (uint32_t)x, (uint32_t)y,
                                     (uint32_t)(uintptr_t)title);
}

void p_wm_destroy_window(void *win) {
  syscall1(SYS_WM_DESTROY_WINDOW, (uint32_t)(uintptr_t)win);
}

void p_wm_mark_dirty() { syscall0(SYS_WM_MARK_DIRTY); }

void p_wm_get_pos(void *win, int *x, int *y) {
  uint32_t ret = (uint32_t)syscall1(SYS_WM_GET_POS, (uint32_t)(uintptr_t)win);
  if (x)
    *x = (int)(ret >> 16);
  if (y)
    *y = (int)(ret & 0xFFFF);
}

void p_wm_get_size(void *win, int *w, int *h) {
  uint32_t ret = (uint32_t)syscall1(SYS_WM_GET_SIZE, (uint32_t)(uintptr_t)win);
  if (w)
    *w = (int)(ret >> 16);
  if (h)
    *h = (int)(ret & 0xFFFF);
}

void p_draw_rect_fill(void *win, int x, int y, int w, int h, uint32_t color) {
  syscall6(SYS_FLY_DRAW_RECT_FILL, (uint32_t)(uintptr_t)win, (uint32_t)x,
           (uint32_t)y, (uint32_t)w, (uint32_t)h, color);
}

void p_draw_text(void *win, int x, int y, const char *text, uint32_t color) {
  syscall5(SYS_FLY_DRAW_TEXT, (uint32_t)(uintptr_t)win, (uint32_t)x,
           (uint32_t)y, (uint32_t)(uintptr_t)text, color);
}

int p_draw_bmp(void *win, int x, int y, const char *path) {
  /* x and y are currently ignored by the kernel implementation which draws at
     0,0, but we pass them for future compatibility */
  (void)x;
  (void)y;
  return syscall2(SYS_FLY_DRAW_BMP, (uint32_t)(uintptr_t)win,
                  (uint32_t)(uintptr_t)path);
}

int p_draw_bmp_fit(void *win, const char *path) {
  return syscall2(SYS_FLY_DRAW_BMP_FIT, (uint32_t)(uintptr_t)win,
                  (uint32_t)(uintptr_t)path);
}

int p_get_event(p_input_event_t *ev) {
  return syscall1(SYS_GET_EVENT, (uint32_t)(uintptr_t)ev);
}

void p_sleep(uint32_t ms) { syscall1(SYS_SLEEP, ms); }
void p_yield() { syscall0(SYS_YIELD); }

void p_get_time(p_time_t *t) {
  uint32_t ret = (uint32_t)syscall0(SYS_GET_TIME);
  if (t) {
    t->hour = (uint8_t)(ret >> 16);
    t->minute = (uint8_t)(ret >> 8);
    t->second = (uint8_t)(ret & 0xFF);
  }
}

int p_get_launch_arg(char *buf, uint32_t size) {
  return syscall2(SYS_GET_LAUNCH_ARG, (uint32_t)(uintptr_t)buf, size);
}

int p_exec_command_capture(const char *line, char *out, uint32_t out_cap) {
  return syscall3(SYS_CMD_EXEC_CAPTURE, (uint32_t)(uintptr_t)line,
                  (uint32_t)(uintptr_t)out, out_cap);
}

int p_open(const char *path, int flags) {
  return syscall2(SYS_OPEN, (uint32_t)(uintptr_t)path, (uint32_t)flags);
}

int p_read(int fd, void *buf, uint32_t size) {
  return syscall3(SYS_READ, (uint32_t)fd, (uint32_t)(uintptr_t)buf, size);
}

void p_close(int fd) { syscall1(SYS_CLOSE, (uint32_t)fd); }
