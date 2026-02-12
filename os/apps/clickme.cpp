#include "../user/libpetal/include/petal.h"

/* Forward declarations */
typedef struct {
  int x, y, w, h;
  const char *label;
  uint32_t color;
  uint32_t hover_color;
  int is_hovered;
} button_t;

static int button_contains(button_t *btn, int px, int py);
static void button_draw(void *win, button_t *btn);

/* Force C linkage for the entry point */
extern "C" {
__attribute__((section(".text._start"))) void _start();
}

void _start() {
  p_write("[APP] ClickMe started\n");
  /* Create main window */
  void *win = p_wm_create_window(300, 200, 100, 100, "Click Me!");

  if (!win) {
    p_write("[APP] Error: Could not create window\n");
    p_exit(1);
  }

  /* Create button */
  button_t btn;
  btn.x = 75;
  btn.y = 70;
  btn.w = 150;
  btn.h = 60;
  btn.label = "Click Me!";
  btn.color = 0x4A90E2;       /* Blue */
  btn.hover_color = 0x357ABD; /* Darker blue */
  btn.is_hovered = 0;

  /* Popup state */
  void *popup = 0;
  int popup_shown = 0;

  /* Initial draw */
  p_draw_rect_fill(win, 0, 0, 300, 200, 0xF0F0F0); /* Light gray background */
  p_draw_text(win, 60, 20, "Welcome to Click Me!", 0x333333);
  button_draw(win, &btn);
  p_wm_mark_dirty();

  /* Event loop */
  p_input_event_t ev;
  int running = 1;

  while (running) {
    if (p_get_event(&ev)) {
      if (ev.type == P_INPUT_MOUSE_MOVE) {
        /* Check if hovering over button */
        int was_hovered = btn.is_hovered;
        btn.is_hovered = button_contains(&btn, ev.mouse_x, ev.mouse_y);

        /* Redraw if hover state changed */
        if (was_hovered != btn.is_hovered) {
          p_draw_rect_fill(win, 0, 0, 300, 200, 0xF0F0F0);
          p_draw_text(win, 60, 20, "Welcome to Click Me!", 0x333333);
          button_draw(win, &btn);
          p_wm_mark_dirty();
        }
      } else if (ev.type == P_INPUT_MOUSE_CLICK && ev.pressed) {
        /* Check if clicked on button */
        if (button_contains(&btn, ev.mouse_x, ev.mouse_y)) {
          if (!popup_shown) {
            /* Create popup window */
            popup = p_wm_create_window(250, 150, 150, 150, "Popup!");

            /* Draw popup content */
            if (popup) {
              p_draw_rect_fill(popup, 0, 0, 250, 150, 0xFFFFFF);
              p_draw_text(popup, 50, 40, "You clicked the button!", 0x000000);
              p_draw_text(popup, 30, 70, "This is a popup window!", 0x666666);

              /* Draw close button in popup */
              p_draw_rect_fill(popup, 75, 100, 100, 30,
                               0xE74C3C); /* Red button */
              p_draw_text(popup, 100, 108, "Close", 0xFFFFFF);

              p_wm_mark_dirty();
              popup_shown = 1;
            }
          }
        }

        /* Check if clicked close button in popup */
        if (popup_shown && popup) {
          /* Close button is at (75, 100, 100, 30) in popup coordinates */
          /* We need to check relative to popup window */
          /* For simplicity, we'll close on any click in popup */
          p_wm_destroy_window(popup);
          popup = 0;
          popup_shown = 0;
          p_wm_mark_dirty();
        }
      } else if (ev.type == P_INPUT_KEYBOARD && ev.pressed) {
        /* Press ESC or 'q' to quit */
        if (ev.keycode == 0x01 || ev.keycode == 0x10) { /* ESC or Q */
          running = 0;
        }
      }
    } else {
      /* No events, sleep a bit to allow other tasks to run and reduce CPU usage
       */
      p_sleep(10);
    }
  }

  /* Cleanup */
  if (popup) {
    p_wm_destroy_window(popup);
  }
  p_wm_destroy_window(win);
  p_exit(0);
}

/* Check if point is inside button */
static int button_contains(button_t *btn, int px, int py) {
  return px >= btn->x && px < btn->x + btn->w && py >= btn->y &&
         py < btn->y + btn->h;
}

/* Draw a button */
static void button_draw(void *win, button_t *btn) {
  uint32_t color = btn->is_hovered ? btn->hover_color : btn->color;

  /* Draw button background */
  p_draw_rect_fill(win, btn->x, btn->y, btn->w, btn->h, color);

  /* Draw button border */
  p_draw_rect_fill(win, btn->x, btn->y, btn->w, 2, 0x000000); /* Top */
  p_draw_rect_fill(win, btn->x, btn->y + btn->h - 2, btn->w, 2,
                   0x000000);                                 /* Bottom */
  p_draw_rect_fill(win, btn->x, btn->y, 2, btn->h, 0x000000); /* Left */
  p_draw_rect_fill(win, btn->x + btn->w - 2, btn->y, 2, btn->h,
                   0x000000); /* Right */

  /* Draw button text (centered) */
  int text_x = btn->x + (btn->w / 2) - 30; /* Rough centering */
  int text_y = btn->y + (btn->h / 2) - 4;
  p_draw_text(win, text_x, text_y, btn->label, 0xFFFFFF);
}
