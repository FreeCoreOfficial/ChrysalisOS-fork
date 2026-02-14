#include "../user/libpetal/include/petal.h"

static int g_w = 200;
static int g_h = 100;

static void itoa(int n, char *s) {
  int i = 0;
  if (n == 0) {
    s[i++] = '0';
  } else {
    while (n > 0) {
      s[i++] = (n % 10) + '0';
      n /= 10;
    }
  }
  s[i] = '\0';
  /* Reverse */
  for (int j = 0; j < i / 2; j++) {
    char tmp = s[j];
    s[j] = s[i - j - 1];
    s[i - j - 1] = tmp;
  }
}

static void format_time(p_time_t *t, char *buf) {
  /* HH:MM:SS */
  if (t->hour < 10) {
    buf[0] = '0';
    itoa(t->hour, buf + 1);
  } else {
    itoa(t->hour, buf);
  }
  buf[2] = ':';
  if (t->minute < 10) {
    buf[3] = '0';
    itoa(t->minute, buf + 4);
  } else {
    itoa(t->minute, buf + 3);
  }
  buf[5] = ':';
  if (t->second < 10) {
    buf[6] = '0';
    itoa(t->second, buf + 7);
  } else {
    itoa(t->second, buf + 6);
  }
  buf[8] = '\0';
}

static void redraw_clock(void *win, const char *time_str) {
  int text_w = 8 * 8;
  int text_x = (g_w - text_w) / 2;
  int text_y = (g_h - 16) / 2;
  if (text_x < 0)
    text_x = 0;
  if (text_y < 0)
    text_y = 0;

  p_draw_rect_fill(win, 0, 0, g_w, g_h, 0x000000);
  p_draw_text(win, text_x, text_y, time_str, 0x00FF00);
  p_wm_mark_dirty();
}

int main() {
  p_write("[APP] Clock Petal started\n");
  void *win = p_wm_create_window(200, 100, 200, 200, "Clock");

  if (!win) {
    return 1;
  }

  p_time_t last_t = {255, 255, 255};
  char time_str[16];
  time_str[0] = '\0';
  p_input_event_t ev;
  int need_redraw = 1;
  p_wm_get_size(win, &g_w, &g_h);
  p_get_time(&last_t);
  format_time(&last_t, time_str);

  int running = 1;
  while (running) {
    p_time_t current_t;
    p_get_time(&current_t);

    int polled_w = 0;
    int polled_h = 0;
    p_wm_get_size(win, &polled_w, &polled_h);
    if (polled_w < 120)
      polled_w = 120;
    if (polled_h < 80)
      polled_h = 80;
    if (polled_w != g_w || polled_h != g_h) {
      g_w = polled_w;
      g_h = polled_h;
      need_redraw = 1;
    }

    if (current_t.second != last_t.second) {
      last_t = current_t;
      format_time(&current_t, time_str);
      need_redraw = 1;
    }

    while (p_get_event(&ev)) {
      if (ev.type == P_INPUT_WINDOW_RESIZE) {
        g_w = ev.mouse_x;
        g_h = ev.mouse_y;
        if (g_w < 120)
          g_w = 120;
        if (g_h < 80)
          g_h = 80;
        need_redraw = 1;
      } else if (ev.type == P_INPUT_KEYBOARD && ev.pressed) {
        if (ev.keycode == 'q' || ev.keycode == 0x1B) {
          running = 0;
        }
      }
    }

    if (need_redraw && time_str[0]) {
      redraw_clock(win, time_str);
      need_redraw = 0;
    }

    p_sleep(50);
  }

  p_wm_destroy_window(win);
  return 0;
}
