#include "../../../mem/kmalloc.h"
#include "../../../string.h"
#include "../../../time/timer.h"
#include "../draw.h"
#include "../theme.h"
#include "widgets.h"

typedef struct {
  char buffer[256];
  int len;
  bool password;
} textbox_data_t;

static void textbox_draw(fly_widget_t *w, surface_t *surf, int x, int y) {
  textbox_data_t *d = (textbox_data_t *)w->internal_data;
  fly_theme_t *th = theme_get();

  /* Draw background and border */
  fly_draw_rect_fill(surf, x, y, w->w, w->h, 0xFFFFFFFF); /* White background */
  fly_draw_rect_outline(surf, x, y, w->w, w->h, th->color_lo_1);

  /* Draw text */
  char display[256];
  if (d->password) {
    for (int i = 0; i < d->len; i++)
      display[i] = '*';
    display[d->len] = 0;
  } else {
    strcpy(display, d->buffer);
  }

  fly_draw_text(surf, x + 4, y + 4, display, 0xFF000000); /* Black text */

  /* Draw cursor if focused and blink is on */
  if (w->focused) {
    if ((timer_uptime_ms() / 500) % 2) {
      int cursor_x = x + 4 + (d->len * 8);
      fly_draw_text(surf, cursor_x, y + 4, "|", 0xFF000000);
    }
  }

  /* Draw cursor if focused */
  /* We need access to the context to check if we are focused,
     but we can just assume w == context->focused_widget if we had the context.
     Alternatively, we could add a 'focused' flag to fly_widget_t.
     Let's add a focused flag to fly_widget_t for simplicity in drawing.
  */
}

static bool textbox_event(fly_widget_t *w, fly_event_t *e) {
  textbox_data_t *d = (textbox_data_t *)w->internal_data;

  if (e->type == FLY_EVENT_KEY_DOWN) {
    if (e->keycode == '\b') {
      if (d->len > 0) {
        d->len--;
        d->buffer[d->len] = 0;
        return true;
      }
    } else if (e->keycode >= 32 && e->keycode <= 126) {
      if (d->len < 255) {
        d->buffer[d->len++] = (char)e->keycode;
        d->buffer[d->len] = 0;
        return true;
      }
    }
  }
  return false;
}

fly_widget_t *fly_textbox_create(int w, bool password) {
  fly_widget_t *widget = fly_widget_create();
  if (!widget)
    return NULL;

  textbox_data_t *d = (textbox_data_t *)kmalloc(sizeof(textbox_data_t));
  if (d) {
    memset(d->buffer, 0, sizeof(d->buffer));
    d->len = 0;
    d->password = password;
    widget->internal_data = d;
  }

  widget->on_draw = textbox_draw;
  widget->on_event = textbox_event;
  widget->w = w;
  widget->h = 24;
  widget->bg_color = 0xFFFFFFFF;
  widget->fg_color = 0xFF000000;

  return widget;
}

const char *fly_textbox_get_text(fly_widget_t *w) {
  if (!w || !w->internal_data)
    return "";
  textbox_data_t *d = (textbox_data_t *)w->internal_data;
  return d->buffer;
}

void fly_textbox_set_text(fly_widget_t *w, const char *text) {
  if (!w || !w->internal_data || !text)
    return;
  textbox_data_t *d = (textbox_data_t *)w->internal_data;
  strncpy(d->buffer, text, 255);
  d->len = strlen(d->buffer);
}
