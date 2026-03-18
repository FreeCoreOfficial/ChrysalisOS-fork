/* user/libpetal/include/petal.h */
#ifndef PETAL_H
#define PETAL_H

#include "../../../kernel/include/chrysalis/syscall_nums.h"
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Event Types */
typedef enum {
  P_INPUT_KEYBOARD,
  P_INPUT_MOUSE,
  P_INPUT_MOUSE_MOVE,
  P_INPUT_MOUSE_CLICK,
  P_INPUT_WINDOW_RESIZE
} p_input_type_t;

typedef struct {
  p_input_type_t type;
  uint32_t keycode;
  uint8_t pressed;
  int32_t mouse_x;
  int32_t mouse_y;
} p_input_event_t;

typedef struct {
  uint8_t hour;
  uint8_t minute;
  uint8_t second;
} p_time_t;

/* System Calls */
void p_exit(int code);
void p_write(const char *s);
void *p_wm_create_window(int w, int h, int x, int y, const char *title);
void p_wm_destroy_window(void *win);
void p_wm_mark_dirty();
void p_wm_get_pos(void *win, int *x, int *y);
void p_wm_get_size(void *win, int *w, int *h);

void p_draw_rect_fill(void *win, int x, int y, int w, int h, uint32_t color);
void p_draw_text(void *win, int x, int y, const char *text, uint32_t color);
int p_draw_bmp(void *win, int x, int y, const char *path);
int p_draw_bmp_fit(void *win, const char *path);
int p_get_event(p_input_event_t *ev);

void p_sleep(uint32_t ms);
void p_yield();
void p_get_time(p_time_t *t);
int p_get_launch_arg(char *buf, uint32_t size);
int p_exec_command_capture(const char *line, char *out, uint32_t out_cap);
int p_user_is_logged(void);

int p_open(const char *path, int flags);
int p_read(int fd, void *buf, uint32_t size);
void p_close(int fd);

#ifdef __cplusplus
}
#endif

#endif
