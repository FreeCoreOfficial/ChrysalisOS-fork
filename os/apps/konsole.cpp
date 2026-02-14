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

static void putchar(char c) {
  if (c == '\n') {
    cursor_x = 0;
    cursor_y++;
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

static void print(const char *s) {
  while (*s) {
    putchar(*s++);
  }
}

void _start() {
  p_write("[APP] Konsole Petal started\n");
  void *win = p_wm_create_window(COLS * CHAR_W + 20, ROWS * CHAR_H + 20, 50, 50,
                                 "Konsole");

  if (!win) {
    p_exit(1);
  }

  clear_screen();
  print("ChrysalisOS Konsole v0.1\n");
  print("> ");
  draw_screen(win);
  p_wm_mark_dirty();

  p_input_event_t ev;
  int running = 1;
  while (running) {
    if (p_get_event(&ev)) {
      if (ev.type == P_INPUT_KEYBOARD && ev.pressed) {
        if (ev.keycode == 0x01) { /* ESC */
          running = 0;
        } else if (ev.keycode == '\b') {
          if (cursor_x > 2) { /* Don't delete prompt */
            cursor_x--;
            screen[cursor_y][cursor_x] = ' ';
          }
        } else if (ev.keycode == '\n') {
          print("\nCommand not found.\n> ");
        } else if (ev.keycode >= 32 && ev.keycode < 127) {
          putchar((char)ev.keycode);
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
