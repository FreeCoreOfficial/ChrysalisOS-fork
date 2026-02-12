#pragma once
#include "../colors/cl.h"
#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
  char c;
  cl_color_t attr;
} console_cell_t;

void fb_cons_init(void);
void fb_cons_putc(char c);
void fb_cons_puts(const char *s);
void fb_cons_clear(void);
void fb_cons_scroll(int lines);

/* Set current text attribute (color) */
void fb_cons_set_attr(cl_color_t attr);

/* Set cursor position */
void fb_cons_set_cursor(uint32_t x, uint32_t y);

/* Enable/Disable visual cursor */
void fb_cons_set_cursor_visible(bool visible);

/* VT Integration API */
void fb_cons_get_dims(uint32_t *cols, uint32_t *rows);

/* Swaps the active buffer and cursor pointers used by the renderer */
void fb_cons_set_state(console_cell_t *new_buf, uint32_t *new_cx,
                       uint32_t *new_cy);
void fb_cons_redraw(void);

#ifdef __cplusplus
}
#endif