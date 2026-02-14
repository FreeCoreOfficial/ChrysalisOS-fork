#include "../user/libpetal/include/petal.h"

static void redraw(void *win, int win_w, int win_h, const char *bmp_path) {
  int rc = p_draw_bmp_fit(win, bmp_path);
  if (rc != 0) {
    p_draw_rect_fill(win, 0, 0, win_w, win_h, 0x222222);
  }

  p_draw_rect_fill(win, 0, 0, win_w, 36, 0x101010);
  p_draw_text(win, 10, 8, bmp_path, 0xDDDDDD);
  if (rc != 0) {
    p_draw_text(win, 10, 22, "Failed to load BMP", 0xFF6666);
  } else {
    p_draw_text(win, 10, 22, "Aspect fit", 0x66FF66);
  }

  p_wm_mark_dirty();
}

int main() {
  p_write("[APP] Image Viewer Petal started\n");
  void *win = p_wm_create_window(400, 300, 150, 150, "Image Viewer");

  if (!win) {
    p_exit(1);
  }

  char launch_path[256];
  const char *bmp_path = "/system/apps/icons/img.bmp";
  int arg_len = p_get_launch_arg(launch_path, sizeof(launch_path));
  if (arg_len > 0 && launch_path[0]) {
    bmp_path = launch_path;
  }

  int win_w = 400;
  int win_h = 300;
  p_wm_get_size(win, &win_w, &win_h);
  redraw(win, win_w, win_h, bmp_path);

  p_input_event_t ev;
  int running = 1;
  while (running) {
    if (p_get_event(&ev)) {
      if (ev.type == P_INPUT_WINDOW_RESIZE) {
        win_w = ev.mouse_x;
        win_h = ev.mouse_y;
        if (win_w < 120)
          win_w = 120;
        if (win_h < 80)
          win_h = 80;
        redraw(win, win_w, win_h, bmp_path);
      } else if (ev.type == P_INPUT_KEYBOARD && ev.pressed) {
        if (ev.keycode == 'q' || ev.keycode == 0x1B) {
          running = 0;
        }
      }
    } else {
      p_sleep(50);
    }
  }

  p_wm_destroy_window(win);
  return 0;
}
