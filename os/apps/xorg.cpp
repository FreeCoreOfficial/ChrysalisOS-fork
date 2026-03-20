#include "../user/libpetal/include/petal.h"
#include <stdint.h>

#define MAX_XWINS 8

typedef struct {
  int x, y, w, h;
  const char *title;
  uint32_t color;
  int alive;
} xwin_t;

static int clampi(int v, int lo, int hi) {
  if (v < lo)
    return lo;
  if (v > hi)
    return hi;
  return v;
}

static void window_to_local(void *win, const p_input_event_t *ev, int *lx,
                            int *ly) {
  int wx = 0;
  int wy = 0;
  p_wm_get_pos(win, &wx, &wy);
  *lx = ev->mouse_x - wx;
  *ly = ev->mouse_y - wy;
}

static void draw_title_bar(void *win, const xwin_t *xw, int focused) {
  uint32_t title_bg = focused ? 0x2A76D2 : 0x3A3A3A;
  p_draw_rect_fill(win, xw->x, xw->y, xw->w, 20, title_bg);
  p_draw_text(win, xw->x + 6, xw->y + 4, xw->title,
              focused ? 0xFFFFFF : 0xDDDDDD);
  /* close button */
  int bx = xw->x + xw->w - 18;
  p_draw_rect_fill(win, bx, xw->y + 2, 14, 14, 0xB84848);
  p_draw_text(win, bx + 4, xw->y + 4, "X", 0xFFFFFF);
}

static void draw_xwin(void *win, const xwin_t *xw, int focused) {
  if (!xw->alive)
    return;
  p_draw_rect_fill(win, xw->x, xw->y, xw->w, xw->h, xw->color);
  draw_title_bar(win, xw, focused);
  p_draw_rect_fill(win, xw->x, xw->y, xw->w, 1, 0x000000);
  p_draw_rect_fill(win, xw->x, xw->y + xw->h - 1, xw->w, 1, 0x000000);
  p_draw_rect_fill(win, xw->x, xw->y, 1, xw->h, 0x000000);
  p_draw_rect_fill(win, xw->x + xw->w - 1, xw->y, 1, xw->h, 0x000000);
}

static void render(void *win, xwin_t *wins, int count, int focused, int sw,
                   int sh) {
  p_draw_rect_fill(win, 0, 0, sw, sh, 0x1B1F2A);
  p_draw_rect_fill(win, 0, 0, sw, 22, 0x12151C);
  p_draw_text(win, 10, 4, "Xorg minimal - light WM", 0xE0E0E0);
  p_draw_text(win, sw - 220, 4, "N: new window  Q: exit", 0xA0A0A0);

  for (int i = 0; i < count; i++) {
    draw_xwin(win, &wins[i], i == focused);
  }
  p_wm_mark_dirty();
}

static int hit_title_bar(const xwin_t *xw, int lx, int ly) {
  return lx >= xw->x && lx < xw->x + xw->w && ly >= xw->y &&
         ly < xw->y + 20;
}

static int hit_close_btn(const xwin_t *xw, int lx, int ly) {
  int bx = xw->x + xw->w - 18;
  return lx >= bx && lx < bx + 14 && ly >= xw->y + 2 && ly < xw->y + 16;
}

static int hit_window(const xwin_t *xw, int lx, int ly) {
  return lx >= xw->x && lx < xw->x + xw->w && ly >= xw->y &&
         ly < xw->y + xw->h;
}

static void add_window(xwin_t *wins, int *count, int sw, int sh) {
  if (*count >= MAX_XWINS)
    return;
  xwin_t *xw = &wins[*count];
  int idx = *count;
  xw->w = clampi(220 + idx * 10, 220, sw - 40);
  xw->h = clampi(140 + idx * 10, 140, sh - 60);
  xw->x = 30 + idx * 20;
  xw->y = 40 + idx * 20;
  xw->title = (idx % 2) ? "XTerm" : "XApp";
  xw->color = (idx % 2) ? 0xEEEEEE : 0xE6E6FF;
  xw->alive = 1;
  (*count)++;
}

int main() {
  p_write("[APP] Xorg minimal started\n");
  void *win = p_wm_create_window(900, 600, 60, 60, "Xorg");
  if (!win) {
    p_write("[APP] Xorg: failed to create window\n");
    p_exit(1);
  }

  int sw = 900;
  int sh = 600;
  p_wm_get_size(win, &sw, &sh);

  xwin_t wins[MAX_XWINS];
  for (int i = 0; i < MAX_XWINS; i++)
    wins[i].alive = 0;
  int win_count = 0;
  add_window(wins, &win_count, sw, sh);
  add_window(wins, &win_count, sw, sh);
  int focused = 0;

  int dragging = 0;
  int drag_idx = -1;
  int drag_off_x = 0;
  int drag_off_y = 0;

  render(win, wins, win_count, focused, sw, sh);

  p_input_event_t ev;
  int running = 1;
  while (running) {
    if (p_get_event(&ev)) {
      if (ev.type == P_INPUT_WINDOW_RESIZE) {
        sw = ev.mouse_x;
        sh = ev.mouse_y;
        if (sw < 320)
          sw = 320;
        if (sh < 200)
          sh = 200;
        render(win, wins, win_count, focused, sw, sh);
      } else if (ev.type == P_INPUT_MOUSE_CLICK && ev.pressed) {
        int lx = 0;
        int ly = 0;
        window_to_local(win, &ev, &lx, &ly);
        for (int i = win_count - 1; i >= 0; i--) {
          if (!wins[i].alive)
            continue;
          if (hit_window(&wins[i], lx, ly)) {
            focused = i;
            if (hit_close_btn(&wins[i], lx, ly)) {
              wins[i].alive = 0;
            } else if (hit_title_bar(&wins[i], lx, ly)) {
              dragging = 1;
              drag_idx = i;
              drag_off_x = lx - wins[i].x;
              drag_off_y = ly - wins[i].y;
            }
            render(win, wins, win_count, focused, sw, sh);
            break;
          }
        }
      } else if (ev.type == P_INPUT_MOUSE_CLICK && !ev.pressed) {
        dragging = 0;
        drag_idx = -1;
      } else if (ev.type == P_INPUT_MOUSE_MOVE) {
        if (dragging && drag_idx >= 0) {
          int lx = 0;
          int ly = 0;
          window_to_local(win, &ev, &lx, &ly);
          wins[drag_idx].x = clampi(lx - drag_off_x, 2, sw - 50);
          wins[drag_idx].y = clampi(ly - drag_off_y, 24, sh - 40);
          render(win, wins, win_count, focused, sw, sh);
        }
      } else if (ev.type == P_INPUT_KEYBOARD && ev.pressed) {
        if (ev.keycode == 'q' || ev.keycode == 'Q' || ev.keycode == 0x01) {
          running = 0;
        } else if (ev.keycode == 'n' || ev.keycode == 'N') {
          add_window(wins, &win_count, sw, sh);
          focused = win_count - 1;
          render(win, wins, win_count, focused, sw, sh);
        }
      }
    } else {
      p_sleep(10);
    }
  }

  p_wm_destroy_window(win);
  return 0;
}
