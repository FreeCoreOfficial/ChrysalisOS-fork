#include "../../mem/kmalloc.h"
#include "flyui.h"
#include <stddef.h>

extern void serial(const char *fmt, ...);
/* extern void flyui_surface_clear(surface_t* surf, uint32_t color); */

flyui_context_t *flyui_init(surface_t *surface) {
  flyui_context_t *ctx = (flyui_context_t *)kmalloc(sizeof(flyui_context_t));
  if (!ctx)
    return NULL;

  ctx->surface = surface;
  ctx->root = NULL;
  ctx->focused_widget = NULL;

  serial("[FLYUI] Initialized context for surface 0x%x\n", surface);
  return ctx;
}

void flyui_set_root(flyui_context_t *ctx, fly_widget_t *root) {
  if (ctx)
    ctx->root = root;
}

static void render_widget_recursive(fly_widget_t *w, surface_t *surf, int x_off,
                                    int y_off) {
  if (!w || !w->visible)
    return;

  /* Calculate absolute position */
  int abs_x = x_off + w->x;
  int abs_y = y_off + w->y;

  /* Draw self */
  if (w->on_draw) {
    w->on_draw(w, surf, abs_x, abs_y);
  }

  /* Draw children (reverse order if prepended? No, just iterate) */
  /* Note: If we prepend children, first_child is top-most logically but drawn
     first? We'll assume standard iteration for now. */
  fly_widget_t *c = w->first_child;
  while (c) {
    render_widget_recursive(c, surf, abs_x, abs_y);
    c = c->next_sibling;
  }
}

void flyui_render(flyui_context_t *ctx) {
  if (!ctx || !ctx->surface)
    return;

  if (ctx->root) {
    render_widget_recursive(ctx->root, ctx->surface, 0, 0);
  }
}

/* Hit test recursive */
static fly_widget_t *hit_test(fly_widget_t *w, int mx, int my, int x_off,
                              int y_off) {
  if (!w || !w->visible)
    return NULL;

  int abs_x = x_off + w->x;
  int abs_y = y_off + w->y;

  /* Check if point is inside this widget */
  if (mx >= abs_x && mx < abs_x + w->w && my >= abs_y && my < abs_y + w->h) {

    /* Check children first (top-most in Z-order) */
    fly_widget_t *hit = NULL;
    fly_widget_t *c = w->first_child;
    while (c) {
      fly_widget_t *res = hit_test(c, mx, my, abs_x, abs_y);
      /* Later children are drawn on top, so they take precedence */
      if (res)
        hit = res;
      c = c->next_sibling;
    }

    if (hit)
      return hit;
    return w; /* No child hit, so it's us */
  }

  return NULL;
}

bool flyui_dispatch_event(flyui_context_t *ctx, fly_event_t *event) {
  if (!ctx || !ctx->root)
    return false;

  /* Keyboard events always go to the focused widget */
  if (event->type == FLY_EVENT_KEY_DOWN || event->type == FLY_EVENT_KEY_UP) {
    if (ctx->focused_widget && ctx->focused_widget->on_event) {
      return ctx->focused_widget->on_event(ctx->focused_widget, event);
    }
    return false;
  }

  /* Mouse events use hit testing */
  fly_widget_t *target = hit_test(ctx->root, event->mx, event->my, 0, 0);

  if (event->type == FLY_EVENT_MOUSE_DOWN) {
    if (ctx->focused_widget) {
      ctx->focused_widget->focused = false;
    }
    ctx->focused_widget = target;
    if (ctx->focused_widget) {
      ctx->focused_widget->focused = true;
    }
  }

  if (target && target->on_event) {
    return target->on_event(target, event);
  }
  return false;
}
