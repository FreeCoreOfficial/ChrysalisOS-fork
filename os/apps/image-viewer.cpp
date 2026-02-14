#include "../user/libpetal/include/petal.h"

/* Standard main entry point */
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

  /* Draw background and status */
  p_draw_rect_fill(win, 0, 0, 400, 300, 0x333333);
  p_draw_text(win, 10, 10, "Loading BMP:", 0xFFFFFF);
  p_draw_text(win, 10, 24, bmp_path, 0xAAAAAA);

  if (p_draw_bmp(win, 0, 0, bmp_path) != 0) {
    p_draw_text(win, 10, 44, "Failed to load BMP:", 0xFF3333);
    p_draw_text(win, 10, 58, bmp_path, 0xFF7777);
  } else {
    p_draw_text(win, 10, 280, "Image loaded:", 0x00FF00);
    p_draw_text(win, 100, 280, bmp_path, 0x99FF99);
  }

  p_wm_mark_dirty();

  p_input_event_t ev;
  int running = 1;
  while (running) {
    if (p_get_event(&ev)) {
      if (ev.type == P_INPUT_KEYBOARD && ev.pressed) {
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
