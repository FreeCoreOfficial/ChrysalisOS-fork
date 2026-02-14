#include "../user/libpetal/include/petal.h"

/* Force C linkage for the entry point */
extern "C" {
__attribute__((section(".text._start"))) void _start();
}

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

void _start() {
  p_write("[APP] Clock Petal started\n");
  void *win = p_wm_create_window(200, 100, 200, 200, "Clock");

  if (!win) {
    p_exit(1);
  }

  p_time_t last_t = {255, 255, 255};
  char time_str[16];
  p_input_event_t ev;

  int running = 1;
  while (running) {
    p_time_t current_t;
    p_get_time(&current_t);

    if (current_t.second != last_t.second) {
      last_t = current_t;
      format_time(&current_t, time_str);

      /* Redraw */
      p_draw_rect_fill(win, 0, 0, 200, 100, 0x000000); /* Black background */
      p_draw_text(win, 50, 40, time_str, 0x00FF00);    /* Green text */
      p_wm_mark_dirty();
    }

    /* Check for events non-blocking */
    while (p_get_event(&ev)) {
      if (ev.type == P_INPUT_KEYBOARD && ev.pressed) {
        if (ev.keycode == 'q' || ev.keycode == 0x1B) { /* 'q' or ESC */
          running = 0;
        }
      }
    }

    p_sleep(100);
  }

  p_wm_destroy_window(win);
  p_exit(0);
}
