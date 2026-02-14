#include "../user/libpetal/include/petal.h"

#define MAX_COLS 120
#define MAX_ROWS 64
#define MIN_COLS 20
#define MIN_ROWS 6
#define CHAR_W 8
#define CHAR_H 16
#define PAD_X 10
#define PAD_Y 10

static char screen[MAX_ROWS][MAX_COLS + 1];
static int cols = 60;
static int rows = 20;
static int win_w = 60 * CHAR_W + PAD_X * 2;
static int win_h = 20 * CHAR_H + PAD_Y * 2;
static int cursor_x = 0;
static int cursor_y = 0;
static char cmdline[256];
static int cmd_len = 0;

static int clampi(int v, int lo, int hi) {
  if (v < lo)
    return lo;
  if (v > hi)
    return hi;
  return v;
}

static void clear_row(int y) {
  if (y < 0 || y >= MAX_ROWS)
    return;
  for (int x = 0; x < MAX_COLS; x++) {
    screen[y][x] = ' ';
  }
  screen[y][MAX_COLS] = '\0';
}

static void clear_screen() {
  for (int y = 0; y < MAX_ROWS; y++) {
    clear_row(y);
  }
}

static void scroll_visible() {
  for (int y = 0; y < rows - 1; y++) {
    for (int x = 0; x < MAX_COLS; x++) {
      screen[y][x] = screen[y + 1][x];
    }
  }
  clear_row(rows - 1);
  cursor_y = rows - 1;
  if (cursor_y < 0)
    cursor_y = 0;
}

static void apply_resize(int new_w, int new_h) {
  if (new_w > 0)
    win_w = new_w;
  if (new_h > 0)
    win_h = new_h;

  int new_cols = (win_w - PAD_X * 2) / CHAR_W;
  int new_rows = (win_h - PAD_Y * 2) / CHAR_H;
  new_cols = clampi(new_cols, MIN_COLS, MAX_COLS);
  new_rows = clampi(new_rows, MIN_ROWS, MAX_ROWS);

  cols = new_cols;
  rows = new_rows;

  if (cursor_x >= cols)
    cursor_x = cols - 1;
  if (cursor_x < 0)
    cursor_x = 0;

  while (cursor_y >= rows) {
    scroll_visible();
  }
}

static void draw_screen(void *win) {
  char line[MAX_COLS + 1];
  p_draw_rect_fill(win, 0, 0, win_w, win_h, 0x000000);
  for (int y = 0; y < rows; y++) {
    for (int x = 0; x < cols; x++) {
      line[x] = screen[y][x];
    }
    line[cols] = '\0';
    p_draw_text(win, PAD_X, PAD_Y + y * CHAR_H, line, 0x00FF00);
  }
}

static void ks_putchar(char c) {
  if (c == '\n') {
    cursor_x = 0;
    cursor_y++;
  } else if (c == '\r') {
    cursor_x = 0;
  } else {
    if (cursor_x >= cols) {
      cursor_x = 0;
      cursor_y++;
    }
    if (cursor_y >= rows) {
      scroll_visible();
    }
    if (cursor_y >= 0 && cursor_y < rows && cursor_x >= 0 && cursor_x < cols) {
      screen[cursor_y][cursor_x++] = c;
    }
  }

  if (cursor_x >= cols) {
    cursor_x = 0;
    cursor_y++;
  }

  if (cursor_y >= rows) {
    scroll_visible();
  }
}

static void print_str(const char *s) {
  while (*s) {
    ks_putchar(*s++);
  }
}

static void print_prompt() { print_str("> "); }

static void exec_current_command() {
  char output[4096];

  cmdline[cmd_len] = '\0';
  ks_putchar('\n');

  if (cmd_len > 0) {
    int n = p_exec_command_capture(cmdline, output, sizeof(output));
    if (n > 0) {
      if (n >= (int)sizeof(output))
        n = (int)sizeof(output) - 1;
      output[n] = 0;
      print_str(output);
      if (n > 0 && output[n - 1] != '\n') {
        ks_putchar('\n');
      }
    } else if (n < 0) {
      print_str("Command execution error.\n");
    }
  }

  cmd_len = 0;
  cmdline[0] = '\0';
  print_prompt();
}

int main() {
  p_write("[APP] Konsole Petal started\n");
  void *win = p_wm_create_window(win_w, win_h, 50, 50, "Konsole");

  if (!win) {
    return 1;
  }

  p_wm_get_size(win, &win_w, &win_h);
  apply_resize(win_w, win_h);

  clear_screen();
  cmdline[0] = '\0';
  cmd_len = 0;
  print_str("ChrysalisOS Konsole v0.2\n");
  print_prompt();
  draw_screen(win);
  p_wm_mark_dirty();

  p_input_event_t ev;
  int running = 1;
  while (running) {
    int polled_w = 0;
    int polled_h = 0;
    p_wm_get_size(win, &polled_w, &polled_h);
    if (polled_w < 120)
      polled_w = 120;
    if (polled_h < 80)
      polled_h = 80;
    if (polled_w != win_w || polled_h != win_h) {
      apply_resize(polled_w, polled_h);
      draw_screen(win);
      p_wm_mark_dirty();
    }

    if (p_get_event(&ev)) {
      if (ev.type == P_INPUT_WINDOW_RESIZE) {
        apply_resize(ev.mouse_x, ev.mouse_y);
        draw_screen(win);
        p_wm_mark_dirty();
      } else if (ev.type == P_INPUT_KEYBOARD && ev.pressed) {
        if (ev.keycode == 0x01 || ev.keycode == 0x1B) { /* ESC */
          running = 0;
        } else if (ev.keycode == '\b' || ev.keycode == 0x08) {
          if (cmd_len > 0) {
            cmd_len--;
            cmdline[cmd_len] = '\0';
            if (cursor_x > 0) {
              cursor_x--;
              screen[cursor_y][cursor_x] = ' ';
            } else if (cursor_y > 0) {
              cursor_y--;
              cursor_x = cols - 1;
              screen[cursor_y][cursor_x] = ' ';
            }
          }
        } else if (ev.keycode == '\n' || ev.keycode == '\r') {
          exec_current_command();
        } else if (ev.keycode >= 32 && ev.keycode < 127) {
          if (cmd_len < (int)sizeof(cmdline) - 1) {
            char c = (char)ev.keycode;
            cmdline[cmd_len++] = c;
            ks_putchar(c);
          }
        }
        draw_screen(win);
        p_wm_mark_dirty();
      }
    } else {
      p_sleep(50);
    }
  }

  p_wm_destroy_window(win);
  return 0;
}
