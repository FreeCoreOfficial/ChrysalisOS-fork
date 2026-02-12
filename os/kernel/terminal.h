// kernel/terminal/terminal.h
#pragma once
#include "video/surface.h"
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

void terminal_putchar(char c);
void terminal_writestring(const char *s);
void terminal_clear();
void terminal_init();
void terminal_set_cursor_position(int x, int y);
void terminal_set_cursor_visible(bool visible);

/* Switch backend to Framebuffer Console */
void terminal_set_backend_fb(bool active);

/* Global rendering control (for GUI mode switch) */
void terminal_set_rendering(bool enabled);

/* Terminal Modes */
typedef enum {
  TERMINAL_MODE_VGA = 0,
  TERMINAL_MODE_FB,
  TERMINAL_MODE_WINDOW
} terminal_mode_t;

void terminal_set_surface(surface_t *s);
void terminal_set_rect(int x, int y, int w, int h);

/* Dirty flag API */
bool terminal_is_dirty(void);
void terminal_clear_dirty(void);

/* ===== NEW: printf support ===== */
void terminal_printf(const char *fmt, ...);
void terminal_vprintf(const char *fmt, void *va); /* intern */

/* print value as hex (no 0x prefix), trimmed leading zeros */
void terminal_writehex(uint32_t v);

/* Set current text attribute (fg/bg). Applies to window mode too. */
void terminal_set_text_attr(uint8_t attr);

/* ===== Piping / Redirection Support ===== */

/* Redirect output to a memory buffer instead of screen */
void terminal_start_capture(char *buf, size_t max_len, size_t *out_len);
void terminal_end_capture(void);

/* Set input source for commands that read from stdin (pipe) */
void terminal_set_input(const char *buf, size_t len);
int terminal_read_char(void); /* Returns char or -1 if EOF/Empty */

#ifdef __cplusplus
}
#endif
