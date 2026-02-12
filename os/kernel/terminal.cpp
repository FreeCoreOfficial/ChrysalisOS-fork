// kernel/terminal/terminal.cpp
#include "terminal.h"
#include "arch/i386/io.h"
#include "colors/cl.h"
#include "drivers/serial.h"
#include "string.h"
#include "video/fb_console.h"
#include "video/font8x16.h"
#include "video/surface.h"
#include "vt/vt.h"
#include <stdarg.h>
#include <stdint.h>

extern "C" void serial(const char *fmt, ...);

static uint16_t *vga = (uint16_t *)0xB8000;
static int row = 0;
static int col = 0;
static uint8_t term_attr = 0x0F; /* light gray on black */
static bool use_fb_console = false;
static terminal_mode_t term_mode = TERMINAL_MODE_VGA;
static surface_t *term_surface = NULL;
static int term_x = 0;
static int term_y = 0;
static int term_w = 0;
static int term_h = 0;
static bool term_dirty = true;
static bool term_rendering_enabled = true;
static uint32_t term_win_fg = 0xFFFDF8F0; /* Off-white */
static uint32_t term_win_bg = 0xFF12171D; /* Chrysalis Slate */

/* Redirection State */
static char *capture_buf = 0;
static size_t capture_max = 0;
static size_t *capture_written = 0;

static const char *input_buf = 0;
static size_t input_len = 0;
static size_t input_pos = 0;

static void scroll() {
  if (row < 25)
    return;

  for (int y = 1; y < 25; y++)
    for (int x = 0; x < 80; x++)
      vga[(y - 1) * 80 + x] = vga[y * 80 + x];

  for (int x = 0; x < 80; x++)
    vga[24 * 80 + x] = ' ' | (term_attr << 8);

  row = 24;
}

/* Window Mode Helpers */
static void term_window_scroll() {
  if (!term_surface)
    return;
  uint32_t pitch = term_surface->width;
  uint32_t *pixels = term_surface->pixels;
  int total_rows = term_h / 16;

  /* Fast path: Full-width scroll */
  if (term_x == 0 && (uint32_t)term_w == pitch) {
    /* Move all rows except the first (total_rows-1) * 16 scanlines */
    size_t move_size = (total_rows - 1) * 16 * pitch * 4;
    memmove(pixels + (term_y * pitch), pixels + ((term_y + 16) * pitch),
            move_size);

    /* Clear bottom row */
    uint32_t *bottom_row = pixels + (term_y + (total_rows - 1) * 16) * pitch;
    for (uint32_t i = 0; i < 16 * pitch; i++)
      bottom_row[i] = term_win_bg;
  } else {
    /* Slow path: Scroll only the rect area (already implemented) */
    for (int y = 1; y < total_rows; y++) {
      int dst_y = term_y + (y - 1) * 16;
      int src_y = term_y + y * 16;
      for (int i = 0; i < 16; i++) {
        uint32_t *dst_row = pixels + (dst_y + i) * pitch + term_x;
        uint32_t *src_row = pixels + (src_y + i) * pitch + term_x;
        memmove(dst_row, src_row, term_w * 4);
      }
    }
    /* Clear bottom line */
    int bottom_y = term_y + (total_rows - 1) * 16;
    for (int i = 0; i < 16; i++) {
      uint32_t *row_ptr = pixels + (bottom_y + i) * pitch + term_x;
      for (int x = 0; x < term_w; x++)
        row_ptr[x] = term_win_bg;
    }
  }

  row = total_rows - 1;
}

static void __attribute__((unused)) term_window_putpixel(int x, int y,
                                                         uint32_t color) {
  if (!term_surface)
    return;
  int px = term_x + x;
  int py = term_y + y;
  if (px < 0 || py < 0 || px >= (int)term_surface->width ||
      py >= (int)term_surface->height)
    return;
  if (x >= term_w || y >= term_h)
    return;

  term_surface->pixels[py * term_surface->width + px] = color;
}

static void term_window_draw_char(char c, int x, int y) {
  if (!term_surface)
    return;
  int px = term_x + x;
  int py = term_y + y;
  uint32_t pitch = term_surface->width;
  uint32_t *pixels = term_surface->pixels;

  if (px < 0 || py < 0 || px + 8 > (int)term_surface->width ||
      py + 16 > (int)term_surface->height)
    return;
  if (x + 8 > term_w || y + 16 > term_h)
    return;

  const uint8_t *glyph = &font8x16[(uint8_t)c * 16];
  for (int i = 0; i < 16; i++) {
    uint32_t *row_ptr = pixels + (py + i) * pitch + px;
    uint8_t glyph_row = glyph[i];
    /* Unrolled/optimized loop for 8 bits */
    row_ptr[0] = (glyph_row & 0x80) ? term_win_fg : term_win_bg;
    row_ptr[1] = (glyph_row & 0x40) ? term_win_fg : term_win_bg;
    row_ptr[2] = (glyph_row & 0x20) ? term_win_fg : term_win_bg;
    row_ptr[3] = (glyph_row & 0x10) ? term_win_fg : term_win_bg;
    row_ptr[4] = (glyph_row & 0x08) ? term_win_fg : term_win_bg;
    row_ptr[5] = (glyph_row & 0x04) ? term_win_fg : term_win_bg;
    row_ptr[6] = (glyph_row & 0x02) ? term_win_fg : term_win_bg;
    row_ptr[7] = (glyph_row & 0x01) ? term_win_fg : term_win_bg;
  }
}

extern "C" void terminal_set_backend_fb(bool active) {
  use_fb_console = active;
  term_mode = active ? TERMINAL_MODE_FB : TERMINAL_MODE_VGA;
  serial("[TERM] Mode set to %s\n", active ? "FB" : "VGA");
}

extern "C" void terminal_set_rendering(bool enabled) {
  term_rendering_enabled = enabled;
}

extern "C" bool terminal_is_dirty(void) { return term_dirty; }

extern "C" void terminal_clear_dirty(void) { term_dirty = false; }

extern "C" void terminal_clear() {
  if (term_mode == TERMINAL_MODE_WINDOW && term_surface) {
    /* Clear only the rect */
    uint32_t pitch = term_surface->width;
    uint32_t *pixels = term_surface->pixels;
    for (int y = 0; y < term_h; y++) {
      uint32_t *row_ptr = pixels + (term_y + y) * pitch + term_x;
      for (int x = 0; x < term_w; x++)
        row_ptr[x] = term_win_bg;
    }
    row = 0;
    col = 0;
    term_dirty = true;
    return;
  }
  if (use_fb_console) {
    fb_cons_clear();
    return;
  }

  for (int i = 0; i < 80 * 25; i++)
    vga[i] = ' ' | (term_attr << 8);

  row = 0;
  col = 0;
}

extern "C" void terminal_putchar(char c) {
  /* Global rendering lock for GUI mode */
  if (!term_rendering_enabled)
    return;

  /* Handle Output Redirection (Piping) */
  if (capture_buf) {
    if (capture_written && *capture_written < capture_max) {
      capture_buf[(*capture_written)++] = c;
    }
    return; /* Do not print to screen when piped */
  }

  if (term_mode == TERMINAL_MODE_WINDOW && term_surface) {
    int max_cols = term_w / 8;
    int max_rows = term_h / 16;

    if (c == '\n') {
      row++;
      col = 0;
      if (row >= max_rows)
        term_window_scroll();
      term_dirty = true;
      return;
    }
    if (c == '\r') {
      col = 0;
      return;
    }
    if (c == '\b') {
      if (col > 0) {
        col--;
        term_window_draw_char(' ', col * 8, row * 16);
      }
      term_dirty = true;
      return;
    }

    term_window_draw_char(c, col * 8, row * 16);
    col++;
    if (col >= max_cols) {
      col = 0;
      row++;
      if (row >= max_rows)
        term_window_scroll();
    }
    /* We defer term_dirty = true for bulk string operations elsewhere if
     * possible, but for single characters we still need it. */
    term_dirty = true;
    return;
  }

  if (use_fb_console) {
    /* Route through VT subsystem to update active buffer */
    vt_putc(c);
    return;
  }

  if (c == '\n') {
    row++;
    col = 0;
    scroll();
    return;
  }

  if (c == '\r') {
    col = 0;
    return;
  }

  if (c == '\b') {
    if (col > 0)
      col--;
    return;
  }

  vga[row * 80 + col] = c | (term_attr << 8);
  col++;

  if (col >= 80) {
    col = 0;
    row++;
    scroll();
  }
}

extern "C" void terminal_writestring(const char *s) {
  if (term_mode == TERMINAL_MODE_WINDOW) {
    int max_cols = term_w / 8;
    int max_rows = term_h / 16;
    while (*s) {
      char c = *s++;
      /* Priority 1: Output Redirection (Piping) */
      if (capture_buf) {
        if (capture_written && *capture_written < capture_max) {
          capture_buf[(*capture_written)++] = c;
        }
        continue;
      }
      /* Priority 2: Visual Rendering */
      if (term_rendering_enabled) {
        if (c == '\n') {
          row++;
          col = 0;
          if (row >= max_rows)
            term_window_scroll();
        } else if (c == '\r') {
          col = 0;
        } else if (c == '\b') {
          if (col > 0) {
            col--;
            term_window_draw_char(' ', col * 8, row * 16);
          }
        } else if (c >= ' ') {
          term_window_draw_char(c, col * 8, row * 16);
          col++;
          if (col >= max_cols) {
            col = 0;
            row++;
            if (row >= max_rows)
              term_window_scroll();
          }
        }
      }
    }
    term_dirty = true;
    return;
  }
  if (use_fb_console) {
    fb_cons_puts(s);
    return;
  }

  while (*s)
    terminal_putchar(*s++);
}

extern "C" void terminal_init() {
  terminal_set_text_attr(cl_default());
  terminal_clear();
}

extern "C" void terminal_set_surface(surface_t *s) {
  if (s) {
    term_surface = s;
    term_mode = TERMINAL_MODE_WINDOW;
    term_x = 0;
    term_y = 0;
    term_w = s->width;
    term_h = s->height;
    row = 0;
    col = 0;
    term_dirty = true;
    serial("[TERM] Surface attached (0x%x). Mode: WINDOW\n", s);
  } else {
    term_surface = NULL;
    term_mode = use_fb_console ? TERMINAL_MODE_FB : TERMINAL_MODE_VGA;
    row = 0;
    col = 0;
    term_dirty = true;
    serial("[TERM] Surface detached. Mode: TEXT/FB\n");
  }
}

extern "C" void terminal_set_rect(int x, int y, int w, int h) {
  term_x = x;
  term_y = y;
  term_w = w;
  term_h = h;
  row = 0;
  col = 0;
  term_dirty = true;
}

/* ---------- PRINT HELPERS (expanded) ---------- */

/* print 32-bit hex WITHOUT "0x" prefix, fixed 8 digits */
static void print_hex32(uint32_t v) {
  const char *hex = "0123456789ABCDEF";
  for (int i = 7; i >= 0; i--) {
    terminal_putchar(hex[(v >> (i * 4)) & 0xF]);
  }
}

/* print pointer as 0xXXXXXXXX (32-bit) */
static void print_ptr(uintptr_t p) {
  terminal_writestring("0x");
  /* assume 32-bit guest; print low 32 bits */
  print_hex32((uint32_t)p);
}

/* print unsigned 32-bit decimal */
static void print_uint32(uint32_t v) {
  char buf[12]; /* enough for 32-bit decimal */
  int i = 0;
  if (v == 0) {
    terminal_putchar('0');
    return;
  }
  while (v > 0 && i < (int)sizeof(buf)) {
    buf[i++] = '0' + (v % 10u);
    v /= 10u;
  }
  while (i--)
    terminal_putchar(buf[i]);
}

/* print signed 32-bit decimal */
static void __attribute__((unused)) print_int32(int32_t v) {
  if (v < 0) {
    terminal_putchar('-');
    /* careful with INT_MIN but that's fine for now */
    print_uint32((uint32_t)(-(int64_t)v));
  } else {
    print_uint32((uint32_t)v);
  }
}

/* ---------- vprintf implementation (keeps original void* va_ptr API)
 * ---------- */
/* va_ptr expected to point to a va_list as in previous code */
extern "C" void terminal_vprintf(const char *fmt, void *va_ptr) {
  va_list args;
  /* copy the caller's va_list */
  va_copy(args, *(va_list *)va_ptr);

  /* Optimization: Batch rendering for vprintf */
  bool was_rendering = term_rendering_enabled;
  if (term_mode == TERMINAL_MODE_WINDOW) {
    terminal_set_rendering(false);
  }

  for (; *fmt; fmt++) {
    if (*fmt != '%') {
      terminal_putchar(*fmt);
      continue;
    }

    fmt++;
    if (*fmt == 0)
      break;

    /* Parse flags and width */
    int pad_zero = 0;
    int width = 0;

    while (*fmt == '0') {
      pad_zero = 1;
      fmt++;
    }

    while (*fmt >= '0' && *fmt <= '9') {
      width = width * 10 + (*fmt - '0');
      fmt++;
    }

    /* Parse type */
    switch (*fmt) {
    case 'c': {
      char c = (char)va_arg(args, int);
      terminal_putchar(c);
      break;
    }

    case 's': {
      const char *s = va_arg(args, const char *);
      terminal_writestring(s ? s : "(null)");
      break;
    }

    case 'd': {
      int v = va_arg(args, int);
      /* Simple padding support for decimal not fully implemented here to save
         space, but we consume the modifiers so they don't print garbage. */
      if (v < 0) {
        terminal_putchar('-');
        v = -v;
      }
      print_uint32((uint32_t)v);
      break;
    }

    case 'u': {
      unsigned int v = va_arg(args, unsigned int);
      print_uint32((uint32_t)v);
      break;
    }

    case 'x':
    case 'X': {
      unsigned int v = va_arg(args, unsigned int);

      /* Manual hex formatting with padding */
      char buf[16];
      int i = 0;
      const char *digits =
          (*fmt == 'X') ? "0123456789ABCDEF" : "0123456789abcdef";

      if (v == 0) {
        buf[i++] = '0';
      } else {
        while (v) {
          buf[i++] = digits[v & 0xF];
          v >>= 4;
        }
      }

      /* Pad */
      while (i < width) {
        buf[i++] = pad_zero ? '0' : ' ';
      }

      /* Print reversed */
      while (i > 0) {
        terminal_putchar(buf[--i]);
      }
      break;
    }

    case 'p': {
      void *p = va_arg(args, void *);
      print_ptr((uintptr_t)p);
      break;
    }

    case '%': {
      terminal_putchar('%');
      break;
    }

    default: {
      /* unknown format: print placeholder */
      terminal_putchar('?');
      break;
    }
    }
  }

  va_end(args);
  if (term_mode == TERMINAL_MODE_WINDOW) {
    terminal_set_rendering(was_rendering);
    /* Safe update: mark dirty without recursive printf */
    if (was_rendering)
      term_dirty = true;
  }
}

/* wrapper expected by callers */
extern "C" void terminal_printf(const char *fmt, ...) {
  va_list args;
  va_start(args, fmt);
  terminal_vprintf(fmt, &args);
  va_end(args);
}

/* ---------- NEW: print hex trimmed (no leading zeros) ---------- */
/* prints hex without 0x; trims leading zeros so 0x00008086 -> "8086" */
extern "C" void terminal_writehex(uint32_t v) {
  const char *hex = "0123456789ABCDEF";
  bool started = false;

  for (int i = 7; i >= 0; i--) {
    uint8_t nib = (v >> (i * 4)) & 0xF;
    if (nib != 0 || started || i == 0) {
      started = true;
      terminal_putchar(hex[nib]);
    }
  }
}

extern "C" void terminal_set_text_attr(uint8_t attr) {
  term_attr = attr;
  term_win_fg = cl_rgb(cl_fg(attr));
  term_win_bg = cl_rgb(cl_bg(attr));
  if (use_fb_console) {
    fb_cons_set_attr(attr);
  }
}

/* ===== Piping Implementation ===== */

extern "C" void terminal_start_capture(char *buf, size_t max_len,
                                       size_t *out_len) {
  capture_buf = buf;
  capture_max = max_len;
  capture_written = out_len;
  if (capture_written)
    *capture_written = 0;
}

extern "C" void terminal_end_capture(void) {
  capture_buf = 0;
  capture_max = 0;
  capture_written = 0;
}

extern "C" void terminal_set_input(const char *buf, size_t len) {
  input_buf = buf;
  input_len = len;
  input_pos = 0;
}

extern "C" int terminal_read_char(void) {
  if (input_buf && input_pos < input_len) {
    return (unsigned char)input_buf[input_pos++];
  }
  return -1; /* EOF */
}

extern "C" void terminal_set_cursor_position(int x, int y) {
  col = x;
  row = y;

  if (term_mode == TERMINAL_MODE_VGA) {
    uint16_t pos = row * 80 + col;
    outb(0x3D4, 0x0F);
    outb(0x3D5, (uint8_t)(pos & 0xFF));
    outb(0x3D4, 0x0E);
    outb(0x3D5, (uint8_t)((pos >> 8) & 0xFF));
  } else if (term_mode == TERMINAL_MODE_FB) {
    fb_cons_set_cursor(x, y);
  } else if (term_mode == TERMINAL_MODE_WINDOW) {
    term_dirty = true;
  }
}

extern "C" void terminal_set_cursor_visible(bool visible) {
  if (term_mode == TERMINAL_MODE_FB) {
    fb_cons_set_cursor_visible(visible);
  }
}
