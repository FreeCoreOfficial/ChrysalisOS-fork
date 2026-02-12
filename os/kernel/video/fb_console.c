#include "fb_console.h"
#include "../colors/cl.h"
#include "../drivers/serial.h"
#include "../mem/kmalloc.h"
#include "../string.h"
#include "font8x16.h"
#include "framebuffer.h"
#include "gpu.h"

/* Import serial logging from kernel glue */
extern void serial(const char *fmt, ...);

/* Console State */
static uint32_t cons_width = 0;
static uint32_t cons_height = 0;
static uint32_t cons_pitch = 0;

static uint32_t cursor_x = 0;
static uint32_t cursor_y = 0;
static uint32_t max_cols = 0;
static uint32_t max_rows = 0;

/* Scrollback Configuration */
#define HISTORY_ROWS 400
static console_cell_t *history_buffer = 0;
static int history_head = 0;  /* Index for next write */
static int history_count = 0; /* Total lines in history */
static int view_offset = 0;   /* 0 = live, >0 = scrolled back lines */

/* State pointers (can be swapped by VT) */
static console_cell_t *active_text_buffer = 0;
static uint32_t *active_cursor_x = &cursor_x;
static uint32_t *active_cursor_y = &cursor_y;
static bool cursor_visible = true;

#define FONT_W 8
#define FONT_H 16

static cl_color_t current_attr = 0;

/* Helper to draw a character using GPU ops */
static void draw_char_at(uint32_t cx, uint32_t cy, char c, uint32_t fg,
                         uint32_t bg) {
  gpu_device_t *gpu = gpu_get_primary();
  if (!gpu)
    return;

  const uint8_t *glyph = &font8x16[(uint8_t)c * 16];
  uint32_t start_y = cy * FONT_H;
  uint32_t start_x = cx * FONT_W;

  /* Bounds check */
  if (start_x + FONT_W > gpu->width || start_y + FONT_H > gpu->height)
    return;

  for (int y = 0; y < FONT_H; y++) {
    uint8_t row = glyph[y];
    for (int x = 0; x < FONT_W; x++) {
      uint32_t color = (row & (1 << (7 - x))) ? fg : bg;
      gpu->ops->putpixel(gpu, start_x + x, start_y + y, color);
    }
  }
}

/* Redraw the entire screen from the text buffer */
void fb_cons_redraw(void) {
  if (!active_text_buffer)
    return;

  /* If viewing history, we need to compose the view */
  if (view_offset > 0) {
    for (uint32_t y = 0; y < max_rows; y++) {
      int logical_line = (int)y - view_offset;

      console_cell_t *src_row = 0;

      if (logical_line >= 0) {
        /* Line is in active text buffer */
        src_row = &active_text_buffer[logical_line * max_cols];
      } else {
        /* Line is in history */
        int hist_depth = -logical_line; // 1-based depth
        if (hist_depth <= history_count) {
          int hist_idx =
              (history_head - hist_depth + HISTORY_ROWS) % HISTORY_ROWS;
          src_row = &history_buffer[hist_idx * max_cols];
        }
      }

      for (uint32_t x = 0; x < max_cols; x++) {
        console_cell_t cell =
            src_row ? src_row[x] : (console_cell_t){' ', current_attr};
        uint32_t fg = cl_rgb(cl_fg(cell.attr));
        uint32_t bg = cl_rgb(cl_bg(cell.attr));
        draw_char_at(x, y, cell.c, fg, bg);
      }
    }
    return;
  }

  for (uint32_t y = 0; y < max_rows; y++) {
    for (uint32_t x = 0; x < max_cols; x++) {
      console_cell_t *cell = &active_text_buffer[y * max_cols + x];
      uint32_t fg = cl_rgb(cl_fg(cell->attr));
      uint32_t bg = cl_rgb(cl_bg(cell->attr));
      draw_char_at(x, y, cell->c, fg, bg);
    }
  }
}

/* Scroll the screen up by one line using the text buffer */
static void scroll(void) {
  if (!active_text_buffer)
    return;

  /* Save top line to history before it disappears */
  if (history_buffer) {
    memcpy(&history_buffer[history_head * max_cols], active_text_buffer,
           max_cols * sizeof(console_cell_t));

    history_head = (history_head + 1) % HISTORY_ROWS;
    if (history_count < HISTORY_ROWS) {
      history_count++;
    }

    serial("[FB_CONS] Scroll: pushed line to history (count=%d)\n",
           history_count);
  }

  /* Move text buffer up in RAM (Fast) */
  uint32_t count = (max_rows - 1) * max_cols;
  memmove(active_text_buffer, active_text_buffer + max_cols,
          count * sizeof(console_cell_t));

  /* Clear last row in buffer */
  console_cell_t *last_row = active_text_buffer + count;
  for (uint32_t i = 0; i < max_cols; i++) {
    last_row[i].c = ' ';
    last_row[i].attr = current_attr;
  }

  /* Redraw screen from buffer (Faster and cleaner than VRAM read/write) */
  fb_cons_redraw();
}

/* Draw cursor (block) */
static void draw_cursor(int on) {
  if (!cursor_visible && on)
    return;
  gpu_device_t *gpu = gpu_get_primary();
  if (!gpu)
    return;

  /* Calculate visual position based on scroll offset */
  int visual_y = (int)(*active_cursor_y) + view_offset;

  /* If cursor is pushed off-screen by scrolling back, don't draw it */
  if (visual_y >= (int)max_rows)
    return;

  uint32_t start_y = visual_y * FONT_H;
  uint32_t start_x = (*active_cursor_x) * FONT_W;

  /* Bounds check */
  if (start_x + FONT_W > gpu->width || start_y + FONT_H > gpu->height)
    return;

  /* If turning off, restore character from buffer */
  if (!on) {
    if (active_text_buffer) {
      console_cell_t *cell = &active_text_buffer[(*active_cursor_y) * max_cols +
                                                 (*active_cursor_x)];
      uint32_t fg = cl_rgb(cl_fg(cell->attr));
      uint32_t bg = cl_rgb(cl_bg(cell->attr));
      draw_char_at((*active_cursor_x), visual_y, cell->c, fg, bg);
    } else {
      uint32_t fg = cl_rgb(cl_fg(current_attr));
      uint32_t bg = cl_rgb(cl_bg(current_attr));
      draw_char_at((*active_cursor_x), visual_y, ' ', fg, bg);
    }
    return;
  }

  /* Draw cursor block */
  uint32_t color = 0x00AAAAAA; // Grey block
  for (int y = FONT_H - 2; y < FONT_H; y++) {
    for (int x = 0; x < FONT_W; x++) {
      gpu->ops->putpixel(gpu, start_x + x, start_y + y, color);
    }
  }
}

void fb_cons_init(void) {
  serial("[FB_CONS] Initializing...\n");
  gpu_device_t *gpu = gpu_get_primary();

  if (!gpu) {
    serial("[FB_CONS] Error: No primary GPU available.\n");
    return;
  }

  cons_width = gpu->width;
  cons_height = gpu->height;
  cons_pitch = gpu->pitch;

  max_cols = cons_width / FONT_W;
  max_rows = cons_height / FONT_H;
  cursor_x = 0;
  cursor_y = 0;
  current_attr = cl_default();

  /* Allocate text buffer */
  /* Note: This initial buffer is used for VT0 or boot log until VT init takes
   * over */
  console_cell_t *boot_buf =
      (console_cell_t *)kmalloc(max_cols * max_rows * sizeof(console_cell_t));

  if (history_buffer)
    kfree(history_buffer);
  history_buffer = (console_cell_t *)kmalloc(max_cols * HISTORY_ROWS *
                                             sizeof(console_cell_t));

  if (!boot_buf || !history_buffer) {
    serial("[FB_CONS] Error: Failed to allocate buffers!\n");
    return;
  }
  active_text_buffer = boot_buf;

  /* Initialize text buffer */
  for (uint32_t i = 0; i < max_cols * max_rows; i++) {
    active_text_buffer[i].c = ' ';
    active_text_buffer[i].attr = current_attr;
  }

  history_head = 0;
  history_count = 0;
  view_offset = 0;

  /* Clear screen */
  gpu->ops->clear(gpu, cl_rgb(cl_bg(current_attr)));
  serial("[FB_CONS] Screen cleared.\n");

  draw_cursor(1);
}

/* Internal putc without cursor handling (for bulk updates) */
static void fb_cons_putc_internal(char c) {

  if (c == '\n') {
    *active_cursor_x = 0;
    (*active_cursor_y)++;
    /* Auto-scroll if we hit the bottom */
    if (*active_cursor_y >= max_rows) {
      scroll();
      *active_cursor_y = max_rows - 1;
    }
  } else if (c == '\r') {
    *active_cursor_x = 0;
  } else if (c == '\b') {
    if (*active_cursor_x > 0) {
      (*active_cursor_x)--;
    } else if (*active_cursor_y > 0) {
      (*active_cursor_y)--;
      *active_cursor_x = max_cols - 1;
    }

  } else if (c == '\t') {
    int spaces = 8 - ((*active_cursor_x) % 8);
    for (int i = 0; i < spaces; i++) {
      /* Wrap if needed inside tab loop */
      if (*active_cursor_x >= max_cols) {
        *active_cursor_x = 0;
        (*active_cursor_y)++;
        if (*active_cursor_y >= max_rows) {
          scroll();
          *active_cursor_y = max_rows - 1;
        }
      }

      console_cell_t *cell = &active_text_buffer[(*active_cursor_y) * max_cols +
                                                 (*active_cursor_x)];
      cell->c = ' ';
      cell->attr = current_attr;
      uint32_t fg = cl_rgb(cl_fg(current_attr));
      uint32_t bg = cl_rgb(cl_bg(current_attr));
      draw_char_at(*active_cursor_x, *active_cursor_y, ' ', fg, bg);
      (*active_cursor_x)++;
    }
  } else if (c >= ' ') {
    /* Wrap text if we are at the edge */
    if (*active_cursor_x >= max_cols) {
      *active_cursor_x = 0;
      (*active_cursor_y)++;
      if (*active_cursor_y >= max_rows) {
        scroll();
        *active_cursor_y = max_rows - 1;
      }
    }

    if (*active_cursor_x < max_cols && *active_cursor_y < max_rows) {
      console_cell_t *cell = &active_text_buffer[(*active_cursor_y) * max_cols +
                                                 (*active_cursor_x)];
      cell->c = c;
      cell->attr = current_attr;
      uint32_t fg = cl_rgb(cl_fg(current_attr));
      uint32_t bg = cl_rgb(cl_bg(current_attr));
      draw_char_at(*active_cursor_x, *active_cursor_y, c, fg, bg);
    }
    (*active_cursor_x)++;
  }
}

void fb_cons_putc(char c) {
  if (!gpu_get_primary() || !active_text_buffer)
    return;

  /* Auto-follow: Reset scrollback on input */
  if (view_offset > 0) {
    view_offset = 0;
    fb_cons_redraw();
    serial("[FB_CONS] Auto-follow: snapped to bottom\n");
  }

  /* Hide cursor, draw char, show cursor */
  draw_cursor(0);
  fb_cons_putc_internal(c);
  draw_cursor(1);
}

void fb_cons_puts(const char *s) {
  if (!gpu_get_primary() || !active_text_buffer)
    return;

  if (view_offset > 0) {
    view_offset = 0;
    fb_cons_redraw();
    serial("[FB_CONS] Auto-follow: snapped to bottom\n");
  }

  /* Optimization: Hide cursor ONCE for the whole string */
  draw_cursor(0);
  while (*s) {
    fb_cons_putc_internal(*s++);
  }
  draw_cursor(1);
}

void fb_cons_clear(void) {
  gpu_device_t *gpu = gpu_get_primary();
  if (!gpu || !active_text_buffer)
    return;
  serial("[FB_CONS] Clearing screen...\n");

  /* Clear text buffer */
  for (uint32_t i = 0; i < max_cols * max_rows; i++) {
    active_text_buffer[i].c = ' ';
    active_text_buffer[i].attr = current_attr;
  }

  /* Reset cursor */
  *active_cursor_x = 0;
  *active_cursor_y = 0;

  /* Note: We do NOT clear history on screen clear, similar to Linux terminal */
  view_offset = 0;

  /* Clear screen and redraw cursor */
  gpu->ops->clear(gpu, cl_rgb(cl_bg(current_attr)));
  draw_cursor(1);
  serial("[FB_CONS] Clear done.\n");
}

void fb_cons_scroll(int lines) {
  /* lines < 0: Scroll BACK (view history)
     lines > 0: Scroll FORWARD (view newer) */

  int old_offset = view_offset;

  view_offset -= lines;

  /* Clamp */
  if (view_offset < 0)
    view_offset = 0;
  if (view_offset > (int)history_count)
    view_offset = history_count;

  if (view_offset != old_offset) {
    serial("[FB_CONS] Scroll offset: %d\n", view_offset);
    draw_cursor(0); // Hide cursor at old position
    fb_cons_redraw();
    draw_cursor(1); // Show cursor at new position (if visible)
  }
}

void fb_cons_get_dims(uint32_t *cols, uint32_t *rows) {
  if (cols)
    *cols = max_cols;
  if (rows)
    *rows = max_rows;
}

void fb_cons_set_state(console_cell_t *new_buf, uint32_t *new_cx,
                       uint32_t *new_cy) {
  active_text_buffer = new_buf;
  active_cursor_x = new_cx;
  active_cursor_y = new_cy;
}

void fb_cons_set_attr(cl_color_t attr) { current_attr = attr; }

void fb_cons_set_cursor(uint32_t x, uint32_t y) {
  if (!active_text_buffer)
    return;

  /* Hide current cursor */
  draw_cursor(0);

  /* Update positions (with bounds check) */
  if (x >= max_cols)
    x = max_cols - 1;
  if (y >= max_rows)
    y = max_rows - 1;

  *active_cursor_x = x;
  *active_cursor_y = y;

  /* Draw new cursor */
  draw_cursor(1);
}

void fb_cons_set_cursor_visible(bool visible) {
  if (cursor_visible == visible)
    return;

  if (visible) {
    cursor_visible = true;
    draw_cursor(1);
  } else {
    draw_cursor(0);
    cursor_visible = false;
  }
}