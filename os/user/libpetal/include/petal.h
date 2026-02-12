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
  P_INPUT_MOUSE_CLICK
} p_input_type_t;

typedef struct {
  p_input_type_t type;
  uint32_t keycode;
  uint8_t pressed;
  int32_t mouse_x;
  int32_t mouse_y;
} p_input_event_t;

/* System Calls */
void p_exit(int code);
void p_write(const char *s);
void *p_wm_create_window(int w, int h, int x, int y, const char *title);
void p_wm_destroy_window(void *win);
void p_wm_mark_dirty();

void p_draw_rect_fill(void *win, int x, int y, int w, int h, uint32_t color);
void p_draw_text(void *win, int x, int y, const char *text, uint32_t color);
int p_get_event(p_input_event_t *ev);

void p_sleep(uint32_t ms);
void p_yield();

#ifdef __cplusplus
}
#endif

#endif
