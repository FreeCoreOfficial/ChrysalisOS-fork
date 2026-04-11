#include "win.h"

#include "../drivers/keyboard.h"
#include "../ethernet/net.h"
#include "../input/input.h"
#include "../storage/io_sched.h"
#include "../string.h"
#include "../terminal.h"
#include "../time/timer.h"
#include "../usb/usb_core.h"
#include "../video/font8x16.h"
#include "../video/framebuffer.h"
#include "../video/gpu.h"

extern "C" void yield();
extern "C" void serial(const char *fmt, ...);
extern "C" void wm_cleanup_task_windows(void *task_ptr) { (void)task_ptr; }

namespace {

struct basic_window_t {
  const char *title;
  int x;
  int y;
  int w;
  int h;
  bool visible;
  bool closable;
  uint32_t title_color;
  uint32_t body_color;
};

static bool g_gui_running = false;
static uint32_t g_fb_width = 0;
static uint32_t g_fb_height = 0;
static int g_mouse_x = 120;
static int g_mouse_y = 90;
static int g_drag_window = -1;
static int g_drag_off_x = 0;
static int g_drag_off_y = 0;
static int g_pressed_close = -1;
static int g_window_order[2] = {0, 1};
static basic_window_t g_windows[2];
static char g_status_line[64];
static char g_hint_line[96];

static const uint32_t COL_DESKTOP = 0xFF204A87;
static const uint32_t COL_DESKTOP_STRIPE = 0xFF183A69;
static const uint32_t COL_WINDOW_EDGE = 0xFF111111;
static const uint32_t COL_WINDOW_HILITE = 0xFFF7F1DA;
static const uint32_t COL_TEXT = 0xFF101010;
static const uint32_t COL_TEXT_LIGHT = 0xFFF8F8F8;
static const uint32_t COL_CLOSE = 0xFFB73A3A;
static const uint32_t COL_CLOSE_PRESSED = 0xFF7E2222;
static const uint32_t COL_CURSOR = 0xFFFFFFFF;
static const uint32_t COL_CURSOR_SHADOW = 0xFF101010;
static const int TITLE_H = 24;

static int clampi(int v, int lo, int hi) {
  if (v < lo)
    return lo;
  if (v > hi)
    return hi;
  return v;
}

static void gui_putpixel(int x, int y, uint32_t color) {
  if (x < 0 || y < 0)
    return;
  if ((uint32_t)x >= g_fb_width || (uint32_t)y >= g_fb_height)
    return;
  fb_putpixel((uint32_t)x, (uint32_t)y, color);
}

static void gui_fill_rect(int x, int y, int w, int h, uint32_t color) {
  if (w <= 0 || h <= 0)
    return;
  if (x >= (int)g_fb_width || y >= (int)g_fb_height)
    return;
  if (x + w <= 0 || y + h <= 0)
    return;

  int rx = x;
  int ry = y;
  int rw = w;
  int rh = h;

  if (rx < 0) {
    rw += rx;
    rx = 0;
  }
  if (ry < 0) {
    rh += ry;
    ry = 0;
  }
  if (rx + rw > (int)g_fb_width)
    rw = (int)g_fb_width - rx;
  if (ry + rh > (int)g_fb_height)
    rh = (int)g_fb_height - ry;
  if (rw <= 0 || rh <= 0)
    return;

  fb_draw_rect((uint32_t)rx, (uint32_t)ry, (uint32_t)rw, (uint32_t)rh, color);
}

static void gui_outline_rect(int x, int y, int w, int h, uint32_t color) {
  gui_fill_rect(x, y, w, 1, color);
  gui_fill_rect(x, y + h - 1, w, 1, color);
  gui_fill_rect(x, y, 1, h, color);
  gui_fill_rect(x + w - 1, y, 1, h, color);
}

static void gui_draw_char(int x, int y, char c, uint32_t fg) {
  const uint8_t *glyph = &font8x16[(uint8_t)c * 16];
  for (int gy = 0; gy < 16; ++gy) {
    uint8_t row = glyph[gy];
    for (int gx = 0; gx < 8; ++gx) {
      if (row & (1u << (7 - gx)))
        gui_putpixel(x + gx, y + gy, fg);
    }
  }
}

static void gui_draw_text(int x, int y, const char *text, uint32_t fg) {
  if (!text)
    return;
  int cx = x;
  for (size_t i = 0; text[i]; ++i) {
    if (text[i] == '\n') {
      y += 16;
      cx = x;
      continue;
    }
    gui_draw_char(cx, y, text[i], fg);
    cx += 8;
  }
}

static void gui_draw_desktop(void) {
  fb_clear(COL_DESKTOP);
  for (uint32_t y = 0; y < g_fb_height; y += 32)
    gui_fill_rect(0, (int)y, (int)g_fb_width, 4, COL_DESKTOP_STRIPE);
  gui_draw_text(24, 24, "Chrysalis Primitive GUI", COL_TEXT_LIGHT);
  gui_draw_text(24, 44, "ESC or Q exits. Drag windows by title bar.",
                COL_TEXT_LIGHT);
}

static void window_reset_defaults(void) {
  g_windows[0].title = "System";
  g_windows[0].x = 90;
  g_windows[0].y = 88;
  g_windows[0].w = 420;
  g_windows[0].h = 220;
  g_windows[0].visible = true;
  g_windows[0].closable = true;
  g_windows[0].title_color = 0xFF6B786C;
  g_windows[0].body_color = 0xFFCFC7B8;

  g_windows[1].title = "Status";
  g_windows[1].x = 560;
  g_windows[1].y = 130;
  g_windows[1].w = 250;
  g_windows[1].h = 130;
  g_windows[1].visible = true;
  g_windows[1].closable = true;
  g_windows[1].title_color = 0xFF5A6E8B;
  g_windows[1].body_color = 0xFFD6DCE4;

  g_window_order[0] = 0;
  g_window_order[1] = 1;
  g_drag_window = -1;
  g_pressed_close = -1;
}

static void gui_format_dec(char *buf, size_t cap, int value) {
  if (!buf || cap == 0)
    return;
  char tmp[16];
  int pos = 0;
  bool neg = value < 0;
  unsigned int v = neg ? (unsigned int)(-value) : (unsigned int)value;
  do {
    tmp[pos++] = (char)('0' + (v % 10));
    v /= 10;
  } while (v && pos < (int)sizeof(tmp));
  size_t out = 0;
  if (neg && out + 1 < cap)
    buf[out++] = '-';
  while (pos > 0 && out + 1 < cap)
    buf[out++] = tmp[--pos];
  buf[out] = 0;
}

static void gui_update_status(void) {
  char num1[16];
  char num2[16];
  gui_format_dec(num1, sizeof(num1), g_mouse_x);
  gui_format_dec(num2, sizeof(num2), g_mouse_y);

  g_status_line[0] = 0;
  strcat(g_status_line, "Cursor: ");
  strcat(g_status_line, num1);
  strcat(g_status_line, ",");
  strcat(g_status_line, num2);

  if (g_drag_window >= 0)
    strcpy(g_hint_line, "Dragging active window");
  else if (!g_windows[0].visible && !g_windows[1].visible)
    strcpy(g_hint_line, "All windows closed. Press R to restore.");
  else
    strcpy(g_hint_line, "Click title bar to focus or drag windows.");
}

static void gui_draw_window(const basic_window_t &win, bool focused,
                            bool close_pressed) {
  if (!win.visible)
    return;

  gui_fill_rect(win.x + 3, win.y + 3, win.w, win.h, 0x66000000);
  gui_fill_rect(win.x, win.y, win.w, win.h, win.body_color);
  gui_outline_rect(win.x, win.y, win.w, win.h, COL_WINDOW_EDGE);
  gui_fill_rect(win.x + 1, win.y + 1, win.w - 2, 1, COL_WINDOW_HILITE);
  gui_fill_rect(win.x + 1, win.y + 1, 1, win.h - 2, COL_WINDOW_HILITE);

  uint32_t title = focused ? win.title_color : 0xFF7A7A7A;
  gui_fill_rect(win.x + 1, win.y + 1, win.w - 2, TITLE_H, title);
  gui_draw_text(win.x + 8, win.y + 5, win.title, COL_TEXT_LIGHT);

  if (win.closable) {
    int bx = win.x + win.w - 20;
    int by = win.y + 4;
    gui_fill_rect(bx, by, 14, 14,
                  close_pressed ? COL_CLOSE_PRESSED : COL_CLOSE);
    gui_outline_rect(bx, by, 14, 14, COL_WINDOW_EDGE);
    gui_draw_text(bx + 3, by - 1, "X", COL_TEXT_LIGHT);
  }

  if (strcmp(win.title, "System") == 0) {
    gui_draw_text(win.x + 16, win.y + 40,
                  "Primitive desktop shell", COL_TEXT);
    gui_draw_text(win.x + 16, win.y + 64,
                  "No taskbar. No widgets. No compositor.", COL_TEXT);
    gui_draw_text(win.x + 16, win.y + 88,
                  "Just framebuffer, mouse and basic windows.", COL_TEXT);
    gui_draw_text(win.x + 16, win.y + 128,
                  "R = reset windows", COL_TEXT);
    gui_draw_text(win.x + 16, win.y + 148,
                  "Q / ESC = return to shell", COL_TEXT);
  } else {
    gui_draw_text(win.x + 16, win.y + 40, g_status_line, COL_TEXT);
    gui_draw_text(win.x + 16, win.y + 64, g_hint_line, COL_TEXT);
    gui_draw_text(win.x + 16, win.y + 96, "This is intentionally primitive.",
                  COL_TEXT);
  }
}

static void gui_draw_cursor(void) {
  static const int cursor_points[][2] = {
      {0, 0}, {0, 1}, {0, 2}, {0, 3}, {0, 4}, {0, 5}, {0, 6}, {0, 7},
      {1, 1}, {1, 2}, {1, 3}, {1, 4}, {1, 5}, {1, 6}, {2, 2}, {2, 3},
      {2, 4}, {2, 5}, {3, 3}, {3, 4}, {4, 4}, {4, 5}, {5, 5}, {6, 6},
      {3, 6}, {2, 7}, {1, 8}};
  static const int shadow_points[][2] = {{1, 0}, {2, 1}, {3, 2}, {4, 3},
                                          {5, 4}, {6, 5}, {7, 6}, {2, 8}};

  for (size_t i = 0; i < sizeof(shadow_points) / sizeof(shadow_points[0]); ++i)
    gui_putpixel(g_mouse_x + shadow_points[i][0], g_mouse_y + shadow_points[i][1],
                 COL_CURSOR_SHADOW);
  for (size_t i = 0; i < sizeof(cursor_points) / sizeof(cursor_points[0]); ++i)
    gui_putpixel(g_mouse_x + cursor_points[i][0], g_mouse_y + cursor_points[i][1],
                 COL_CURSOR);
}

static int top_window_at(int x, int y) {
  for (int i = 1; i >= 0; --i) {
    basic_window_t &win = g_windows[g_window_order[i]];
    if (!win.visible)
      continue;
    if (x >= win.x && x < win.x + win.w && y >= win.y && y < win.y + win.h)
      return g_window_order[i];
  }
  return -1;
}

static bool point_in_close(const basic_window_t &win, int x, int y) {
  if (!win.closable)
    return false;
  int bx = win.x + win.w - 20;
  int by = win.y + 4;
  return x >= bx && x < bx + 14 && y >= by && y < by + 14;
}

static bool point_in_title(const basic_window_t &win, int x, int y) {
  return x >= win.x && x < win.x + win.w && y >= win.y && y < win.y + TITLE_H;
}

static void bring_to_front(int idx) {
  if (idx < 0)
    return;
  if (g_window_order[1] == idx)
    return;
  if (g_window_order[0] == idx) {
    g_window_order[0] = g_window_order[1];
    g_window_order[1] = idx;
  }
}

static void render_gui(void) {
  gui_update_status();
  gui_draw_desktop();
  for (int i = 0; i < 2; ++i) {
    int idx = g_window_order[i];
    gui_draw_window(g_windows[idx], i == 1, g_pressed_close == idx);
  }
  gui_draw_cursor();

  gpu_device_t *gpu = gpu_get_primary();
  if (gpu && gpu->ops && gpu->ops->flush)
    gpu->ops->flush(gpu);
}

static void restore_windows(void) { window_reset_defaults(); }

static void handle_keyboard(uint32_t keycode) {
  if (keycode == 27 || keycode == 'q' || keycode == 'Q') {
    g_gui_running = false;
    return;
  }
  if (keycode == 'r' || keycode == 'R') {
    restore_windows();
    return;
  }
  if (keycode == '1') {
    g_windows[0].visible = true;
    bring_to_front(0);
    return;
  }
  if (keycode == '2') {
    g_windows[1].visible = true;
    bring_to_front(1);
    return;
  }
}

static void handle_mouse_click(bool pressed, int x, int y, uint32_t button) {
  if (button != 1)
    return;

  if (pressed) {
    int idx = top_window_at(x, y);
    if (idx < 0)
      return;

    bring_to_front(idx);
    basic_window_t &win = g_windows[idx];
    if (point_in_close(win, x, y)) {
      g_pressed_close = idx;
      return;
    }
    if (point_in_title(win, x, y)) {
      g_drag_window = idx;
      g_drag_off_x = x - win.x;
      g_drag_off_y = y - win.y;
    }
    return;
  }

  if (g_pressed_close >= 0) {
    basic_window_t &win = g_windows[g_pressed_close];
    if (win.visible && point_in_close(win, x, y))
      win.visible = false;
  }

  g_pressed_close = -1;
  g_drag_window = -1;
}

static void handle_mouse_move(int x, int y) {
  g_mouse_x = clampi(x, 0, (int)g_fb_width - 1);
  g_mouse_y = clampi(y, 0, (int)g_fb_height - 1);

  if (g_drag_window >= 0) {
    basic_window_t &win = g_windows[g_drag_window];
    win.x = clampi(g_mouse_x - g_drag_off_x, 8,
                   (int)g_fb_width - win.w - 8);
    win.y = clampi(g_mouse_y - g_drag_off_y, 8,
                   (int)g_fb_height - win.h - 8);
  }
}

} // namespace

extern "C" int cmd_launch_exit(int argc, char **argv) {
  (void)argc;
  (void)argv;
  if (!g_gui_running) {
    terminal_writestring("Primitive GUI is not running.\n");
    return 1;
  }
  g_gui_running = false;
  return 0;
}

extern "C" int cmd_logoff(int argc, char **argv) {
  (void)argc;
  (void)argv;
  terminal_writestring("Logoff is not implemented in the primitive GUI.\n");
  return 1;
}

extern "C" int cmd_launch(int argc, char **argv) {
  (void)argc;
  (void)argv;

  if (g_gui_running) {
    terminal_writestring("Primitive GUI is already running.\n");
    return 1;
  }

  uint32_t pitch = 0;
  uint8_t bpp = 0;
  uint8_t *buffer = NULL;
  fb_get_info(&g_fb_width, &g_fb_height, &pitch, &bpp, &buffer);
  if (!buffer || g_fb_width == 0 || g_fb_height == 0) {
    terminal_writestring("Primitive GUI unavailable: no framebuffer.\n");
    return 1;
  }

  serial("[BASICGUI] Starting primitive GUI...\n");

  g_mouse_x = (int)g_fb_width / 2;
  g_mouse_y = (int)g_fb_height / 2;
  restore_windows();

  terminal_set_cursor_visible(false);
  terminal_set_rendering(false);
  g_gui_running = true;

  render_gui();

  input_event_t ev;
  while (g_gui_running) {
    usb_poll();
    io_sched_poll();
    net_poll();
    ps2_controller_watchdog();

    bool dirty = false;
    while (input_pop(&ev)) {
      if (ev.type == INPUT_KEYBOARD && ev.pressed) {
        handle_keyboard(ev.keycode);
        dirty = true;
      } else if (ev.type == INPUT_MOUSE_MOVE) {
        handle_mouse_move(ev.mouse_x, ev.mouse_y);
        dirty = true;
      } else if (ev.type == INPUT_MOUSE_CLICK) {
        handle_mouse_move(ev.mouse_x, ev.mouse_y);
        handle_mouse_click(ev.pressed, ev.mouse_x, ev.mouse_y, ev.keycode);
        dirty = true;
      }
    }

    if (dirty)
      render_gui();

    yield();
  }

  terminal_set_rendering(true);
  terminal_set_cursor_visible(true);
  terminal_clear();
  serial("[BASICGUI] Primitive GUI stopped.\n");
  return 0;
}

bool win_is_gui_running(void) { return g_gui_running; }
