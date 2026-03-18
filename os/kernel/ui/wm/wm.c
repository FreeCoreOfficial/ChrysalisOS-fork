#include "wm.h"
#include "../../mem/kmalloc.h"
#include "../../sched/pcb.h"
#include "../../string.h"
#include "../../terminal.h"
#include "../../video/compositor.h"
#include "../../video/gpu.h"
#include "../flyui/draw.h"
#include "../flyui/flyui.h"
#include "../flyui/theme.h"
#include "../flyui/widgets/widgets.h"
#include <stddef.h>

/* Import serial logging */
extern void serial(const char *fmt, ...);

#define MAX_WINDOWS 32

static window_t *windows_list = NULL;
static window_t *focused_window = NULL;
static wm_layout_t *current_layout = &wm_layout_floating;
static uint32_t next_win_id = 1;
static bool wm_dirty = true;
static int wm_reserved_bottom = 0;

/* Array for layout processing */
static window_t *win_array[MAX_WINDOWS];
static size_t win_count = 0;

extern void wm_chrome_draw_zig(surface_t *surf, int x, int y, int w,
                               int title_h, uint32_t title_bg);

#define WM_CHROME_MAGIC 0x4348524dU
#define WM_CHROME_BTN_MIN 1
#define WM_CHROME_BTN_MAX 2
#define WM_CHROME_BTN_CLOSE 3

typedef struct wm_chrome {
  uint32_t magic;
  flyui_context_t *ctx;
  fly_widget_t *root;
  fly_widget_t *title_label;
  fly_widget_t *btn_min;
  fly_widget_t *btn_max;
  fly_widget_t *btn_close;
  window_t *win;
  bool focused;
  int last_w;
  int last_h;
  int last_title_h;
} wm_chrome_t;

static void wm_chrome_on_min(fly_widget_t *w) {
  if (!w->parent)
    return;
  wm_chrome_t *c = (wm_chrome_t *)w->parent->internal_data;
  if (c && c->win)
    wm_minimize_window(c->win);
}

static void wm_chrome_on_max(fly_widget_t *w) {
  if (!w->parent)
    return;
  wm_chrome_t *c = (wm_chrome_t *)w->parent->internal_data;
  if (c && c->win)
    wm_toggle_maximize(c->win);
}

static void wm_chrome_on_close(fly_widget_t *w) {
  if (!w->parent)
    return;
  wm_chrome_t *c = (wm_chrome_t *)w->parent->internal_data;
  if (c && c->win) {
    if (c->win->on_close)
      c->win->on_close(c->win);
    else
      wm_destroy_window(c->win);
  }
}

static void chrome_root_draw(fly_widget_t *w, surface_t *surf, int x, int y) {
  wm_chrome_t *c = (wm_chrome_t *)w->internal_data;
  if (!c || !c->win)
    return;

  fly_theme_t *th = theme_get();
  int ww = w->w;
  int title_h = th->title_height;

  uint32_t title_bg =
      c->focused ? th->win_title_active_bg : th->win_title_inactive_bg;
  wm_chrome_draw_zig(surf, x, y, ww, title_h, title_bg);
}

static wm_chrome_t *wm_chrome_peek(window_t *win) {
  if (!win)
    return NULL;
  wm_chrome_t *c = (wm_chrome_t *)win->userdata;
  if (c && c->magic == WM_CHROME_MAGIC)
    return c;
  return NULL;
}

static void chrome_layout(wm_chrome_t *c) {
  if (!c || !c->win || !c->root)
    return;
  if (!c->win->surface)
    return;

  fly_theme_t *th = theme_get();
  int ww = (int)c->win->surface->width;
  int title_h = th->title_height;

  c->root->x = 0;
  c->root->y = 0;
  c->root->w = ww;
  c->root->h = title_h;

  if (c->title_label) {
    c->title_label->x = 8;
    c->title_label->y = 6;
  }

  int btn = 16;
  int pad = 4;
  int by = (title_h - btn) / 2;
  int current_bx = ww - pad - btn;

  if (c->btn_close) {
    c->btn_close->x = current_bx;
    c->btn_close->y = by;
    c->btn_close->w = btn;
    c->btn_close->h = btn;
    current_bx -= (pad + btn);
  }

  if (c->btn_max && c->btn_max->visible) {
    c->btn_max->x = current_bx;
    c->btn_max->y = by;
    c->btn_max->w = btn;
    c->btn_max->h = btn;
    current_bx -= (pad + btn);
  }

  if (c->btn_min && c->btn_min->visible) {
    c->btn_min->x = current_bx;
    c->btn_min->y = by;
    c->btn_min->w = btn;
    c->btn_min->h = btn;
    current_bx -= (pad + btn);
  }

  c->last_w = ww;
  c->last_h = (int)c->win->surface->height;
  c->last_title_h = title_h;
}

static wm_chrome_t *wm_chrome_get(window_t *win) {
  wm_chrome_t *c = wm_chrome_peek(win);
  if (c)
    return c;

  c = (wm_chrome_t *)kmalloc(sizeof(wm_chrome_t));
  if (!c)
    return NULL;
  memset(c, 0, sizeof(wm_chrome_t));
  c->magic = WM_CHROME_MAGIC;
  c->win = win;
  c->ctx = flyui_init(win->surface);
  c->root = fly_widget_create();
  if (!c->ctx || !c->root)
    return c;

  c->root->on_draw = chrome_root_draw;
  c->root->internal_data = c;

  c->title_label = fly_label_create(win->title[0] ? win->title : "Chrysalis");
  c->btn_min = fly_button_create("_");
  c->btn_max = fly_button_create("[]");
  c->btn_close = fly_button_create("X");

  c->btn_min->visible = (win->flags & WIN_FLAG_NO_RESIZE) == 0;
  c->btn_max->visible = (win->flags & WIN_FLAG_NO_MAXIMIZE) == 0 &&
                        (win->flags & WIN_FLAG_NO_RESIZE) == 0;

  fly_button_set_callback(c->btn_min, wm_chrome_on_min);
  fly_button_set_callback(c->btn_max, wm_chrome_on_max);
  fly_button_set_callback(c->btn_close, wm_chrome_on_close);

  fly_widget_add(c->root, c->title_label);
  if (c->btn_min->visible)
    fly_widget_add(c->root, c->btn_min);
  if (c->btn_max->visible)
    fly_widget_add(c->root, c->btn_max);
  fly_widget_add(c->root, c->btn_close);

  flyui_set_root(c->ctx, c->root);
  chrome_layout(c);

  win->userdata = c;
  return c;
}

static void wm_chrome_render(window_t *win, bool focused) {
  wm_chrome_t *c = wm_chrome_get(win);
  if (!c || !c->ctx || !c->root || !win->surface)
    return;

  c->focused = focused;
  c->ctx->surface = win->surface;

  fly_theme_t *th = theme_get();
  if (c->title_label) {
    c->title_label->fg_color =
        focused ? th->win_title_active_fg : th->win_title_inactive_fg;
  }

  if (c->last_w != (int)win->surface->width ||
      c->last_h != (int)win->surface->height ||
      c->last_title_h != th->title_height) {
    chrome_layout(c);
  }
  flyui_render(c->ctx);
}

bool wm_chrome_handle_event(window_t *win, int rel_x, int rel_y, bool pressed) {
  wm_chrome_t *c = wm_chrome_get(win);
  if (!c || !c->ctx || !c->root)
    return false;

  fly_event_t ev;
  ev.mx = rel_x;
  ev.my = rel_y;
  ev.keycode = 0;
  ev.type = pressed ? FLY_EVENT_MOUSE_DOWN : FLY_EVENT_MOUSE_UP;
  bool consumed = flyui_dispatch_event(c->ctx, &ev);

  if (!c->win)
    return true;

  if (consumed || !pressed) {
    flyui_render(c->ctx);
    wm_mark_dirty();
  }
  return consumed;
}

void wm_init(void) {
  windows_list = NULL;
  focused_window = NULL;
  current_layout = &wm_layout_floating;
  win_count = 0;
  wm_dirty = true;
  serial("[WM] Initialized\n");
}

extern bool terminal_is_dirty(void);

void wm_mark_dirty(void) { wm_dirty = true; }

bool wm_is_dirty(void) { return wm_dirty || terminal_is_dirty(); }

void wm_set_reserved_bottom(int pixels) {
  if (pixels < 0)
    pixels = 0;
  wm_reserved_bottom = pixels;
}

int wm_get_reserved_bottom(void) { return wm_reserved_bottom; }

bool wm_window_is_decorated(window_t *win) {
  if (!win)
    return false;
  return (win->flags & WIN_FLAG_NO_DECOR) == 0;
}

void wm_set_window_flags(window_t *win, uint32_t flags) {
  if (!win)
    return;
  win->flags = flags;
}

void wm_set_title(window_t *win, const char *title) {
  if (!win || !title)
    return;
  strncpy(win->title, title, sizeof(win->title) - 1);
  win->title[sizeof(win->title) - 1] = 0;

  wm_chrome_t *c = wm_chrome_peek(win);
  if (c && c->title_label) {
    fly_label_set_text(c->title_label, win->title);
  }
}

void wm_set_on_close(window_t *win, void (*on_close)(window_t *)) {
  if (!win)
    return;
  win->on_close = on_close;
}

void wm_set_on_resize(window_t *win, void (*on_resize)(window_t *)) {
  if (!win)
    return;
  win->on_resize = on_resize;
}

static void wm_emit_resize_event(window_t *win) {
  if (!win || !win->owner)
    return;

  input_event_t ev;
  ev.type = INPUT_WINDOW_RESIZE;
  ev.keycode = 0;
  ev.pressed = true;
  ev.mouse_x = win->w;
  ev.mouse_y = win->h;
  window_push_event(win, &ev);
}

void wm_minimize_window(window_t *win) {
  if (!win || !win->surface)
    return;
  if (win->state == WIN_STATE_MINIMIZED)
    return;

  win->restore_x = win->x;
  win->restore_y = win->y;
  win->restore_w = win->w;
  win->restore_h = win->h;
  win->state = WIN_STATE_MINIMIZED;
  win->surface->visible = false;
  if (focused_window == win)
    focused_window = NULL;
  wm_dirty = true;
}

void wm_restore_window(window_t *win) {
  if (!win || !win->surface)
    return;
  if (win->state == WIN_STATE_MINIMIZED) {
    win->surface->visible = true;
    win->state = WIN_STATE_NORMAL;
    wm_focus_window(win);
    wm_dirty = true;
  }
}

static void wm_maximize_window(window_t *win) {
  if (!win || !win->surface)
    return;
  gpu_device_t *gpu = gpu_get_primary();
  if (!gpu)
    return;

  win->restore_x = win->x;
  win->restore_y = win->y;
  win->restore_w = win->w;
  win->restore_h = win->h;

  int new_w = (int)gpu->width;
  int new_h = (int)gpu->height - wm_reserved_bottom;
  if (new_w < 100)
    new_w = 100;
  if (new_h < 80)
    new_h = 80;

  win->x = 0;
  win->y = 0;
  if (surface_resize(win->surface, (uint32_t)new_w, (uint32_t)new_h,
                     theme_get()->win_bg)) {
    win->w = new_w;
    win->h = new_h;
    if (win->on_resize)
      win->on_resize(win);
    wm_emit_resize_event(win);
  }
  win->state = WIN_STATE_MAXIMIZED;
  wm_dirty = true;
}

static void wm_restore_from_max(window_t *win) {
  if (!win || !win->surface)
    return;
  win->x = win->restore_x;
  win->y = win->restore_y;
  if (surface_resize(win->surface, (uint32_t)win->restore_w,
                     (uint32_t)win->restore_h, theme_get()->win_bg)) {
    win->w = win->restore_w;
    win->h = win->restore_h;
    if (win->on_resize)
      win->on_resize(win);
    wm_emit_resize_event(win);
  }
  win->state = WIN_STATE_NORMAL;
  wm_dirty = true;
}

void wm_toggle_maximize(window_t *win) {
  if (!win)
    return;
  if (win->state == WIN_STATE_MINIMIZED) {
    wm_restore_window(win);
  }
  if (win->state == WIN_STATE_MAXIMIZED) {
    wm_restore_from_max(win);
  } else {
    wm_maximize_window(win);
  }
}

bool wm_resize_window(window_t *win, int w, int h) {
  if (!win || !win->surface)
    return false;
  if (w < 120)
    w = 120;
  if (h < 80)
    h = 80;
  if (!surface_resize(win->surface, (uint32_t)w, (uint32_t)h,
                      theme_get()->win_bg))
    return false;
  win->w = w;
  win->h = h;
  if (win->on_resize)
    win->on_resize(win);
  wm_emit_resize_event(win);
  wm_dirty = true;
  return true;
}

static void rebuild_win_array(void) {
  win_count = 0;
  window_t *cur = windows_list;
  while (cur && win_count < MAX_WINDOWS) {
    win_array[win_count++] = cur;
    cur = cur->next;
  }
}

window_t *wm_create_window(surface_t *surface, int x, int y) {
  if (win_count >= MAX_WINDOWS) {
    serial("[WM] Error: Max windows reached\n");
    return NULL;
  }

  window_t *win = window_create(surface, x, y);
  if (!win)
    return NULL;

  win->id = next_win_id++;

  /* Link window to task for event routing */
  pcb_t *cur = pcb_get_current();
  if (cur && win) {
    win->owner = cur;
  }

  /* Add to list */
  win->next = windows_list;
  windows_list = win;

  rebuild_win_array();

  serial("[WM] Window created id=%u owner=%x\n", win->id, win->owner);

  wm_hooks_t *hooks = wm_get_hooks();
  if (hooks && hooks->on_window_create) {
    hooks->on_window_create(win);
  }

  wm_focus_window(win);
  wm_dirty = true;
  return win;
}

void wm_destroy_window(window_t *win) {
  if (!win)
    return;

  serial("[WM] Destroying window id=%u\n", win->id);

  wm_chrome_t *chrome = wm_chrome_peek(win);
  if (chrome) {
    chrome->win = NULL;
    win->userdata = NULL;
  }

  /* Remove from list */
  if (windows_list == win) {
    windows_list = win->next;
  } else {
    window_t *cur = windows_list;
    while (cur && cur->next != win) {
      cur = cur->next;
    }
    if (cur) {
      cur->next = win->next;
    }
  }

  rebuild_win_array();

  if (focused_window == win) {
    focused_window = windows_list; /* Focus first available or NULL */
    serial("[WM] Focus changed (auto)\n");
  }

  wm_hooks_t *hooks = wm_get_hooks();
  if (hooks && hooks->on_window_destroy) {
    hooks->on_window_destroy(win);
  }

  serial("[WM] Window destroyed id=%u\n", win->id);
  window_destroy(win);
  wm_dirty = true;
}

void wm_focus_window(window_t *win) {
  if (focused_window == win)
    return;

  window_t *old = focused_window;
  focused_window = win;

  /* Move focused window to front (head of list) if not STAY_BOTTOM */
  if (win && windows_list != win && (win->flags & WIN_FLAG_STAY_BOTTOM) == 0) {
    window_t *cur = windows_list;
    window_t *prev = NULL;
    while (cur && cur != win) {
      prev = cur;
      cur = cur->next;
    }
    if (cur) {
      if (prev)
        prev->next = cur->next;
      cur->next = windows_list;
      windows_list = cur;
      rebuild_win_array();
    }
  }

  serial("[WM] Focus changed\n");

  wm_hooks_t *hooks = wm_get_hooks();
  if (hooks && hooks->on_focus_change) {
    hooks->on_focus_change(old, win);
  }
  wm_dirty = true;
}

window_t *wm_get_focused(void) { return focused_window; }

void wm_set_layout(wm_layout_t *layout) {
  if (layout) {
    current_layout = layout;
    serial("[WM] Layout set to %s\n", layout->name);
  }
  wm_dirty = true;
}

window_t *wm_find_window_at(int x, int y) {
  /* Iterate from Newest (Top) to Oldest (Bottom) */
  for (size_t i = 0; i < win_count; i++) {
    window_t *w = win_array[i];
    if (!w || !w->surface)
      continue;
    if (w->state == WIN_STATE_MINIMIZED)
      continue;
    if (!w->surface->visible)
      continue;

    if (x >= w->x && x < w->x + (int)w->surface->width && y >= w->y &&
        y < w->y + (int)w->surface->height) {
      return w;
    }
  }
  return NULL;
}

static void wm_draw_decorations(window_t *w, bool focused) {
  if (!w || !w->surface)
    return;
  if (!wm_window_is_decorated(w))
    return;

  wm_chrome_render(w, focused);

  /* Resize grip */
  if ((w->flags & WIN_FLAG_NO_RESIZE) == 0) {
    fly_theme_t *th = theme_get();
    int ww = (int)w->surface->width;
    int wh = (int)w->surface->height;
    for (int i = 0; i < 8; i++) {
      fly_draw_rect_fill(w->surface, ww - 1 - i, wh - 1, 1, 1, th->color_lo_1);
      fly_draw_rect_fill(w->surface, ww - 1, wh - 1 - i, 1, 1, th->color_lo_1);
    }
  }
}

void wm_render(void) {
  if (!wm_dirty && !terminal_is_dirty())
    return;

  /* 1. Apply Layout */
  if (current_layout && current_layout->apply) {
    current_layout->apply(win_array, win_count);
  }

  /* 2. Draw decorations before render */
  for (size_t i = 0; i < win_count; i++) {
    window_t *w = win_array[i];
    if (!w || !w->surface)
      continue;
    if (w->state == WIN_STATE_MINIMIZED || !w->surface->visible)
      continue;
    wm_draw_decorations(w, w == focused_window);
  }

  /* 3. Prepare Surface List for Compositor (Bottom to Top) */
  surface_t *render_list[MAX_WINDOWS];
  int render_count = 0;

  /* Iterate backwards (Oldest/Bottom -> Newest/Top) for Painter's Algorithm */
  for (int i = (int)win_count - 1; i >= 0; i--) {
    window_t *w = win_array[i];
    if (w->surface) {
      w->surface->x = w->x;
      w->surface->y = w->y;
      render_list[render_count++] = w->surface;
    }
  }

  /* 4. Hooks */
  wm_hooks_t *hooks = wm_get_hooks();
  if (hooks && hooks->on_frame) {
    hooks->on_frame();
  }

  /* 5. Render */
  serial("[WM] Rendering\n");
  compositor_render_surfaces(render_list, render_count);

  wm_dirty = false;
  terminal_clear_dirty();
}

void wm_cleanup_task_windows(void *task_ptr) {
  if (!task_ptr)
    return;
  /* Iterate and destroy all windows owned by this task */
  while (true) {
    bool found = false;
    window_t *cur = windows_list;
    while (cur) {
      if (cur->owner == task_ptr) {
        serial("[WM] Cleanup: Destroying window id=%d for task %x\n", cur->id,
               task_ptr);
        wm_destroy_window(cur);
        found = true;
        break; /* Restart loop as list changed */
      }
      cur = cur->next;
    }
    if (!found)
      break;
  }
}
