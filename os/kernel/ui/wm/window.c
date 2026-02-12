#include "window.h"
#include "../../include/task.h"
#include "../../mem/kmalloc.h"

window_t *window_create(surface_t *surface, int x, int y) {
  window_t *win = (window_t *)kmalloc(sizeof(window_t));
  if (!win)
    return 0;

  win->id = 0; // Assigned by WM
  win->surface = surface;
  win->x = x;
  win->y = y;
  win->w = surface ? surface->width : 0;
  win->h = surface ? surface->height : 0;
  win->z = 0;
  win->flags = 0;
  win->userdata = 0;
  win->state = WIN_STATE_NORMAL;
  win->restore_x = x;
  win->restore_y = y;
  win->restore_w = win->w;
  win->restore_h = win->h;
  win->on_close = 0;
  win->on_resize = 0;
  win->title[0] = 0;
  win->next = 0;

  /* Initialize owner */
  win->owner = 0;

  return win;
}

void window_push_event(window_t *win, input_event_t *ev) {
  if (!win || !win->owner)
    return;
  task_push_event((task_t *)win->owner, ev);
}

int window_pop_event(window_t *win, input_event_t *ev) {
  if (!win || !win->owner)
    return 0;
  return task_pop_event((task_t *)win->owner, ev);
}

void window_destroy(window_t *win) {
  if (win) {
    kfree(win);
  }
}
