#include "../user/libpetal/include/petal.h"

/* Force C linkage for the entry point */
extern "C" {
__attribute__((section(".text._start"))) void _start();
}

#define COLS 60
#define ROWS 20
#define CHAR_W 8
#define CHAR_H 16

static char screen[ROWS][COLS + 1];
static int cursor_x = 0;
static int cursor_y = 0;
static char cmdline[256];
static int cmd_len = 0;

static void clear_screen() {
  for (int y = 0; y < ROWS; y++) {
    for (int x = 0; x < COLS; x++) {
      screen[y][x] = ' ';
    }
    screen[y][COLS] = '\0';
  }
}

static void draw_screen(void *win) {
  p_draw_rect_fill(win, 0, 0, COLS * CHAR_W + 20, ROWS * CHAR_H + 20, 0x000000);
  for (int y = 0; y < ROWS; y++) {
    p_draw_text(win, 10, 10 + y * CHAR_H, screen[y], 0x00FF00);
  }
}

static void ks_putchar(char c) {
  if (c == '\n') {
    cursor_x = 0;
    cursor_y++;
  } else if (c == '\r') {
    cursor_x = 0;
  } else {
    if (cursor_x < COLS) {
      screen[cursor_y][cursor_x++] = c;
    }
  }

  if (cursor_y >= ROWS) {
    /* Scroll */
    for (int y = 0; y < ROWS - 1; y++) {
      for (int x = 0; x < COLS; x++) {
        screen[y][x] = screen[y + 1][x];
      }
    }
    for (int x = 0; x < COLS; x++) {
      screen[ROWS - 1][x] = ' ';
    }
    cursor_y = ROWS - 1;
  }
}

static void print_str(const char *s) {
  while (*s) {
    ks_putchar(*s++);
  }
}

static void exec_current_command() {
  char output[2048];

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
  print_str("> ");
}

void _start() {
  p_write("[APP] Konsole Petal started\n");
  void *win = p_wm_create_window(COLS * CHAR_W + 20, ROWS * CHAR_H + 20, 50, 50,
                                 "Konsole");

  if (!win) {
    p_exit(1);
  }

  clear_screen();
  cmdline[0] = '\0';
  cmd_len = 0;
  print_str("ChrysalisOS Konsole v0.2\n");
  print_str("> ");
  draw_screen(win);
  p_wm_mark_dirty();

  p_input_event_t ev;
  int running = 1;
  while (running) {
    if (p_get_event(&ev)) {
      if (ev.type == P_INPUT_KEYBOARD && ev.pressed) {
        if (ev.keycode == 0x01 || ev.keycode == 0x1B) { /* ESC */
          running = 0;
        } else if (ev.keycode == '\b' || ev.keycode == 0x08) {
          if (cmd_len > 0) {
            cmd_len--;
            cmdline[cmd_len] = '\0';
            cursor_x--;
            screen[cursor_y][cursor_x] = ' ';
          }
        } else if (ev.keycode == '\n') {
          exec_current_command();
        } else if (ev.keycode >= 32 && ev.keycode < 127) {
          if (cmd_len < (int)sizeof(cmdline) - 1 && cursor_x < COLS) {
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
  p_exit(0);
}
