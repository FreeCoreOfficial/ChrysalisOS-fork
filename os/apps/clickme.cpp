#include "../user/libpetal/include/petal.h"

typedef struct {
  int x, y, w, h;
  const char *label;
  uint32_t color;
  uint32_t hover_color;
  int is_hovered;
} button_t;

static int str_len(const char *s) {
  int n = 0;
  while (s && s[n])
    n++;
  return n;
}

static int clampi(int v, int lo, int hi) {
  if (v < lo)
    return lo;
  if (v > hi)
    return hi;
  return v;
}

static int button_contains(button_t *btn, int px, int py) {
  return px >= btn->x && px < btn->x + btn->w && py >= btn->y &&
         py < btn->y + btn->h;
}

static void button_draw(void *win, button_t *btn) {
  uint32_t color = btn->is_hovered ? btn->hover_color : btn->color;

  p_draw_rect_fill(win, btn->x, btn->y, btn->w, btn->h, color);
  p_draw_rect_fill(win, btn->x, btn->y, btn->w, 2, 0x000000);
  p_draw_rect_fill(win, btn->x, btn->y + btn->h - 2, btn->w, 2, 0x000000);
  p_draw_rect_fill(win, btn->x, btn->y, 2, btn->h, 0x000000);
  p_draw_rect_fill(win, btn->x + btn->w - 2, btn->y, 2, btn->h, 0x000000);

  int text_x = btn->x + (btn->w - str_len(btn->label) * 8) / 2;
  int text_y = btn->y + (btn->h - 16) / 2;
  if (text_x < btn->x + 4)
    text_x = btn->x + 4;
  p_draw_text(win, text_x, text_y, btn->label, 0xFFFFFF);
}

static void layout_button(button_t *btn, int win_w, int win_h) {
  int max_w = win_w - 40;
  if (max_w < 80)
    max_w = 80;
  btn->w = clampi(win_w / 2, 100, max_w);
  btn->h = (win_h < 120) ? 36 : 44;
  btn->x = (win_w - btn->w) / 2;
  btn->y = (win_h - btn->h) / 2 + 12;
  if (btn->y < 34)
    btn->y = 34;
}

static void window_to_local(void *win, const p_input_event_t *ev, int *lx,
                            int *ly) {
  int wx = 0;
  int wy = 0;
  p_wm_get_pos(win, &wx, &wy);
  *lx = ev->mouse_x - wx;
  *ly = ev->mouse_y - wy;
}

static void layout_and_redraw(void *win, button_t *btn, int win_w, int win_h) {
  const char *title = "Welcome to Click Me!";
  p_draw_rect_fill(win, 0, 0, win_w, win_h, 0xF0F0F0);
  p_draw_text(win, (win_w - str_len(title) * 8) / 2, 14, title, 0x333333);
  button_draw(win, btn);
  p_wm_mark_dirty();
}

int main() {
  p_write("[APP] ClickMe started\n");
  void *win = p_wm_create_window(300, 200, 100, 100, "Click Me!");

  if (!win) {
    p_write("[APP] Error: Could not create window\n");
    p_exit(1);
  }

  button_t btn;
  btn.label = "Click Me!";
  btn.color = 0x4A90E2;
  btn.hover_color = 0x357ABD;
  btn.is_hovered = 0;

  int win_w = 300;
  int win_h = 200;
  p_wm_get_size(win, &win_w, &win_h);
  layout_button(&btn, win_w, win_h);

  void *popup = 0;
  int popup_shown = 0;

  layout_and_redraw(win, &btn, win_w, win_h);

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
      win_w = polled_w;
      win_h = polled_h;
      layout_button(&btn, win_w, win_h);
      layout_and_redraw(win, &btn, win_w, win_h);
    }

    if (p_get_event(&ev)) {
      if (ev.type == P_INPUT_WINDOW_RESIZE) {
        win_w = ev.mouse_x;
        win_h = ev.mouse_y;
        if (win_w < 120)
          win_w = 120;
        if (win_h < 80)
          win_h = 80;
        layout_button(&btn, win_w, win_h);
        layout_and_redraw(win, &btn, win_w, win_h);
      } else if (ev.type == P_INPUT_MOUSE_MOVE) {
        int lx = 0;
        int ly = 0;
        window_to_local(win, &ev, &lx, &ly);
        int was_hovered = btn.is_hovered;
        btn.is_hovered = button_contains(&btn, lx, ly);
        if (was_hovered != btn.is_hovered) {
          layout_and_redraw(win, &btn, win_w, win_h);
        }
      } else if (ev.type == P_INPUT_MOUSE_CLICK && ev.pressed) {
        int lx = 0;
        int ly = 0;
        int clicked_button = 0;
        window_to_local(win, &ev, &lx, &ly);
        clicked_button = button_contains(&btn, lx, ly);

        if (clicked_button) {
          if (!popup_shown) {
            popup = p_wm_create_window(250, 150, 150, 150, "Popup!");
            if (popup) {
              p_draw_rect_fill(popup, 0, 0, 250, 150, 0xFFFFFF);
              p_draw_text(popup, 50, 40, "You clicked the button!", 0x000000);
              p_draw_text(popup, 30, 70, "This is a popup window!", 0x666666);
              p_draw_rect_fill(popup, 75, 100, 100, 30, 0xE74C3C);
              p_draw_text(popup, 100, 108, "Close", 0xFFFFFF);
              p_wm_mark_dirty();
              popup_shown = 1;
            }
          }
        } else if (popup_shown && popup) {
          p_wm_destroy_window(popup);
          popup = 0;
          popup_shown = 0;
          p_wm_mark_dirty();
        }
      } else if (ev.type == P_INPUT_KEYBOARD && ev.pressed) {
        if (ev.keycode == 0x01 || ev.keycode == 0x10) {
          running = 0;
        }
      }
    } else {
      p_sleep(10);
    }
  }

  if (popup) {
    p_wm_destroy_window(popup);
  }
  p_wm_destroy_window(win);
  return 0;
}
