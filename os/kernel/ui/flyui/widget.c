#include "../../mem/kmalloc.h"
#include "flyui.h"
#include <stddef.h>

fly_widget_t *fly_widget_create(void) {
  fly_widget_t *w = (fly_widget_t *)kmalloc(sizeof(fly_widget_t));
  if (!w)
    return NULL;

  w->x = 0;
  w->y = 0;
  w->w = 10;
  w->h = 10;
  w->bg_color = 0xFFFFFFFF;
  w->fg_color = 0xFF000000;
  w->parent = NULL;
  w->first_child = NULL;
  w->next_sibling = NULL;
  w->internal_data = NULL;
  w->on_draw = NULL;
  w->on_event = NULL;
  w->visible = true;
  w->focused = false;

  return w;
}

void fly_widget_add(fly_widget_t *parent, fly_widget_t *child) {
  if (!parent || !child)
    return;

  child->parent = parent;
  child->next_sibling = NULL;

  if (!parent->first_child) {
    parent->first_child = child;
  } else {
    /* Append to end of list */
    fly_widget_t *c = parent->first_child;
    while (c->next_sibling) {
      c = c->next_sibling;
    }
    c->next_sibling = child;
  }
}

/* Note: Destruction would require recursive free, omitted for minimal
 * implementation */