/* kernel/cmds/write.cpp
 * Full-screen text editor with visual interface
 */

#include "write.h"
#include "../input/input.h"
#include "../mem/kmalloc.h"
#include "../shell/shell.h"
#include "../string.h"
#include "../terminal.h"
#include "../ui/wm/wm.h"
#include "cd.h"
#include "fat.h"
#include <stdarg.h>

/* FAT32 Driver API */
extern "C" int fat32_create_file(const char *path, const void *data,
                                 uint32_t size);
extern "C" int fat32_read_file(const char *path, void *buf, uint32_t max_size);
extern "C" void serial(const char *fmt, ...);

/* VGA Colors */
#define COLOR_BLACK 0
#define COLOR_BLUE 1
#define COLOR_GREEN 2
#define COLOR_CYAN 3
#define COLOR_RED 4
#define COLOR_MAGENTA 5
#define COLOR_BROWN 6
#define COLOR_LIGHTGRAY 7
#define COLOR_DARKGRAY 8
#define COLOR_LIGHTBLUE 9
#define COLOR_LIGHTGREEN 10
#define COLOR_LIGHTCYAN 11
#define COLOR_LIGHTRED 12
#define COLOR_LIGHTMAGENTA 13
#define COLOR_YELLOW 14
#define COLOR_WHITE 15

/* Helper Functions for Terminal Control */
static inline void terminal_set_color(uint8_t fg, uint8_t bg) {
  uint8_t attr = (bg << 4) | (fg & 0x0F);
  terminal_set_text_attr(attr);
}

static int sprintf(char *buf, const char *fmt, ...) {
  va_list args;
  va_start(args, fmt);
  int written = 0;
  while (*fmt) {
    if (*fmt == '%') {
      fmt++;
      if (*fmt == 'd') {
        int val = va_arg(args, int);
        char tmp[12];
        int i = 0;
        if (val == 0)
          buf[written++] = '0';
        else {
          if (val < 0) {
            buf[written++] = '-';
            val = -val;
          }
          while (val > 0) {
            tmp[i++] = '0' + (val % 10);
            val /= 10;
          }
          while (i > 0)
            buf[written++] = tmp[--i];
        }
      } else if (*fmt == 's') {
        const char *str = va_arg(args, const char *);
        while (*str)
          buf[written++] = *str++;
      }
      fmt++;
    } else {
      buf[written++] = *fmt++;
    }
  }
  buf[written] = 0;
  va_end(args);
  return written;
}

#define EDITOR_WIDTH 80
#define EDITOR_HEIGHT 22
#define MAX_LINES 1000
#define MAX_LINE_LEN 160

/* Key definitions (Remapped in driver to avoid Ctrl conflict) */
#define KEY_UP 0x80
#define KEY_DOWN 0x81
#define KEY_LEFT 0x82
#define KEY_RIGHT 0x83
#define KEY_BACKSPACE 0x08
#define KEY_ENTER 0x0A
#define KEY_TAB 0x09
#define CTRL_Q 17
#define CTRL_S 19
#define CTRL_X 24

class Editor {
public:
  Editor() {
    lines = (char **)kmalloc(MAX_LINES * sizeof(char *));
    line_lengths = (int *)kmalloc(MAX_LINES * sizeof(int));
    for (int i = 0; i < MAX_LINES; i++) {
      lines[i] = (char *)kmalloc(MAX_LINE_LEN);
      memset(lines[i], 0, MAX_LINE_LEN);
      line_lengths[i] = 0;
    }
  }

  ~Editor() {
    for (int i = 0; i < MAX_LINES; i++)
      kfree(lines[i]);
    kfree(lines);
    kfree(line_lengths);
  }

  void run(const char *path) {
    filename = path;
    load_file(path);
    terminal_clear();
    draw_all();

    input_event_t ev;
    while (true) {
      bool handled = false;
      while (input_pop(&ev)) {
        if (ev.type == INPUT_KEYBOARD) {
          if (ev.pressed) {
            /* Exit logic */
            if (ev.keycode == CTRL_Q || ev.keycode == CTRL_X)
              goto exit_loop;
            handle_input(ev.keycode);
            handled = true;
          }
        }
      }
      if (handled)
        draw_all();
      else
        asm volatile("hlt");
    }
  exit_loop:
    terminal_set_color(COLOR_WHITE, COLOR_BLACK);
    terminal_clear();
  }

private:
  const char *filename;
  char **lines;
  int *line_lengths;
  int num_lines = 1;
  int cursor_x = 0;
  int cursor_y = 0;
  int scroll_offset = 0;
  int horizontal_scroll = 0;
  bool modified = false;

  void handle_input(uint32_t key) {
    if (key == KEY_UP) {
      if (cursor_y > 0) {
        cursor_y--;
        if (cursor_x > line_lengths[cursor_y])
          cursor_x = line_lengths[cursor_y];
        if (cursor_y < scroll_offset)
          scroll_offset = cursor_y;
      }
    } else if (key == KEY_DOWN) {
      if (cursor_y < num_lines - 1) {
        cursor_y++;
        if (cursor_x > line_lengths[cursor_y])
          cursor_x = line_lengths[cursor_y];
        if (cursor_y >= scroll_offset + EDITOR_HEIGHT)
          scroll_offset = cursor_y - EDITOR_HEIGHT + 1;
      }
    } else if (key == KEY_LEFT) {
      if (cursor_x > 0)
        cursor_x--;
      else if (cursor_y > 0) {
        cursor_y--;
        cursor_x = line_lengths[cursor_y];
        if (cursor_y < scroll_offset)
          scroll_offset = cursor_y;
      }
    } else if (key == KEY_RIGHT) {
      if (cursor_x < line_lengths[cursor_y])
        cursor_x++;
      else if (cursor_y < num_lines - 1) {
        cursor_y++;
        cursor_x = 0;
        if (cursor_y >= scroll_offset + EDITOR_HEIGHT)
          scroll_offset = cursor_y - EDITOR_HEIGHT + 1;
      }
    } else if (key == KEY_BACKSPACE) {
      if (cursor_x > 0) {
        memmove(&lines[cursor_y][cursor_x - 1], &lines[cursor_y][cursor_x],
                line_lengths[cursor_y] - cursor_x);
        line_lengths[cursor_y]--;
        cursor_x--;
        modified = true;
      } else if (cursor_y > 0) {
        int prev_len = line_lengths[cursor_y - 1];
        int curr_len = line_lengths[cursor_y];
        if (prev_len + curr_len < MAX_LINE_LEN) {
          memcpy(&lines[cursor_y - 1][prev_len], lines[cursor_y], curr_len);
          line_lengths[cursor_y - 1] += curr_len;
          for (int i = cursor_y; i < num_lines - 1; i++) {
            memcpy(lines[i], lines[i + 1], MAX_LINE_LEN);
            line_lengths[i] = line_lengths[i + 1];
          }
          num_lines--;
          cursor_y--;
          cursor_x = prev_len;
          modified = true;
          if (cursor_y < scroll_offset)
            scroll_offset = cursor_y;
        }
      }
    } else if (key == KEY_ENTER) {
      if (num_lines < MAX_LINES) {
        for (int i = num_lines; i > cursor_y + 1; i--) {
          memcpy(lines[i], lines[i - 1], MAX_LINE_LEN);
          line_lengths[i] = line_lengths[i - 1];
        }
        int split_at = cursor_x;
        int remaining = line_lengths[cursor_y] - split_at;
        memcpy(lines[cursor_y + 1], &lines[cursor_y][split_at], remaining);
        line_lengths[cursor_y + 1] = remaining;
        line_lengths[cursor_y] = split_at;
        num_lines++;
        cursor_y++;
        cursor_x = 0;
        modified = true;
        if (cursor_y >= scroll_offset + EDITOR_HEIGHT)
          scroll_offset = cursor_y - EDITOR_HEIGHT + 1;
      }
    } else if (key == CTRL_S) {
      save_file(filename);
    } else if (key >= 32 && key < 127) {
      if (line_lengths[cursor_y] < MAX_LINE_LEN - 1) {
        memmove(&lines[cursor_y][cursor_x + 1], &lines[cursor_y][cursor_x],
                line_lengths[cursor_y] - cursor_x);
        lines[cursor_y][cursor_x] = (char)key;
        line_lengths[cursor_y]++;
        cursor_x++;
        modified = true;
      }
    }

    if (cursor_x < horizontal_scroll)
      horizontal_scroll = cursor_x;
    if (cursor_x >= horizontal_scroll + EDITOR_WIDTH)
      horizontal_scroll = cursor_x - EDITOR_WIDTH + 1;
  }

  void draw_all() {
    terminal_set_cursor_visible(false);
    draw_title_bar();
    draw_editor_area();
    draw_status_bar();
    draw_help_bar();
    terminal_set_cursor_visible(true);
    terminal_set_cursor_position(cursor_x - horizontal_scroll,
                                 cursor_y - scroll_offset + 1);
  }

  void draw_title_bar() {
    terminal_set_cursor_position(0, 0);
    terminal_set_color(COLOR_BLACK, COLOR_WHITE);
    char title[EDITOR_WIDTH + 1];
    sprintf(title, " Chrysalis Write v0.2.4 - %s %s", filename,
            modified ? "[MODIFIED]" : "");
    terminal_writestring(title);
    for (int i = strlen(title); i < EDITOR_WIDTH; i++)
      terminal_putchar(' ');
  }

  void draw_editor_area() {
    for (int i = 0; i < EDITOR_HEIGHT; i++) {
      int file_line = i + scroll_offset;
      terminal_set_cursor_position(0, i + 1);
      terminal_set_color(COLOR_WHITE, COLOR_BLACK);
      if (file_line < num_lines) {
        int len = line_lengths[file_line];
        int to_draw = len - horizontal_scroll;
        if (to_draw < 0)
          to_draw = 0;
        if (to_draw > EDITOR_WIDTH)
          to_draw = EDITOR_WIDTH;
        if (to_draw > 0) {
          char tmp[EDITOR_WIDTH + 1];
          memcpy(tmp, lines[file_line] + horizontal_scroll, to_draw);
          tmp[to_draw] = 0;
          terminal_writestring(tmp);
        }
        for (int j = to_draw; j < EDITOR_WIDTH; j++)
          terminal_putchar(' ');
      } else {
        for (int j = 0; j < EDITOR_WIDTH; j++)
          terminal_putchar(' ');
      }
    }
  }

  void draw_status_bar() {
    terminal_set_cursor_position(0, EDITOR_HEIGHT + 1);
    terminal_set_color(COLOR_BLACK, COLOR_WHITE);
    char bar[EDITOR_WIDTH + 1];
    sprintf(bar, " [Line %d/%d, Col %d] %s", cursor_y + 1, num_lines,
            cursor_x + 1, modified ? "*" : " ");
    terminal_writestring(bar);
    for (int i = strlen(bar); i < EDITOR_WIDTH; i++)
      terminal_putchar(' ');
  }

  void draw_help_bar() {
    terminal_set_cursor_position(0, EDITOR_HEIGHT + 2);
    terminal_set_color(COLOR_BLACK, COLOR_LIGHTGRAY);
    const char *help = " ^S Save    ^Q Exit    ^X Exit    Arrows Navigate";
    terminal_writestring(help);
    for (int i = strlen(help); i < EDITOR_WIDTH; i++)
      terminal_putchar(' ');
  }

  void load_file(const char *path) {
    char *buf = (char *)kmalloc(128 * 1024);
    int size = fat32_read_file(path, buf, 128 * 1024);
    if (size >= 0) {
      num_lines = 1;
      int cur_line_pos = 0;
      for (int i = 0; i < size && num_lines < MAX_LINES; i++) {
        if (buf[i] == '\n') {
          line_lengths[num_lines - 1] = cur_line_pos;
          num_lines++;
          cur_line_pos = 0;
        } else if (buf[i] == '\r')
          continue;
        else if (cur_line_pos < MAX_LINE_LEN - 1) {
          lines[num_lines - 1][cur_line_pos++] = buf[i];
        }
      }
      line_lengths[num_lines - 1] = cur_line_pos;
      modified = false;
    }
    kfree(buf);
  }

  void save_file(const char *path) {
    char *buf = (char *)kmalloc(128 * 1024);
    int pos = 0;
    for (int i = 0; i < num_lines; i++) {
      int len = line_lengths[i];
      memcpy(buf + pos, lines[i], len);
      pos += len;
      if (i < num_lines - 1)
        buf[pos++] = '\n';
    }
    fat32_create_file(path, buf, pos);
    modified = false;
    kfree(buf);
  }
};

extern "C" int cmd_write(int argc, char **argv) {
  const char *filename = "untitled.txt";
  if (argc > 1)
    filename = argv[1];
  fat_automount();
  Editor editor;
  editor.run(filename);
  return 0;
}
