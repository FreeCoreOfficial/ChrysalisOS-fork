#include "../user/libpetal/include/petal.h"

/* Standard main entry point */
int main() {
  p_write("[APP] Image Viewer Petal started\n");
  void *win = p_wm_create_window(400, 300, 150, 150, "Image Viewer");

  if (!win) {
    p_exit(1);
  }

  /* Draw background and text */
  p_draw_rect_fill(win, 0, 0, 400, 300, 0x333333);
  p_draw_text(win, 10, 10, "Loading...", 0xFFFFFF);

  /* Load BMP image */
  // Note: We need a sample BMP in the ISO. We don't have one user-accessible
  // yet except in /system/apps/icons? Or we can try to load one of the icons,
  // or the wallpaper if accessible. Let's try to load a known existing file.
  // The Installer puts background.tga, but we only support BMP.
  // Icons are BMP.
  if (p_draw_bmp(win, 0, 0, "/system/apps/icons/img.bmp") != 0) {
    p_draw_text(win, 10, 30, "img.bmp not found!", 0xFF0000);
  } else {
    p_draw_text(win, 10, 280, "Image Loaded", 0x00FF00);
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
