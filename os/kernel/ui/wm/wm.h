#pragma once
#include "window.h"
#include "wm_hooks.h"
#include "wm_layout.h"

#ifdef __cplusplus
extern "C" {
#endif

void wm_init(void);
window_t *wm_create_window(surface_t *surface, int x, int y);
void wm_destroy_window(window_t *win);
void wm_cleanup_task_windows(void *task_ptr);

void wm_focus_window(window_t *win);
window_t *wm_get_focused(void);

void wm_render(void);
void wm_set_layout(wm_layout_t *layout);

/* Hit test: Find window at global coordinates */
window_t *wm_find_window_at(int x, int y);
bool wm_window_is_decorated(window_t *win);
void wm_set_window_flags(window_t *win, uint32_t flags);
void wm_set_title(window_t *win, const char *title);
void wm_set_on_close(window_t *win, void (*on_close)(window_t *));
void wm_set_on_resize(window_t *win, void (*on_resize)(window_t *));
void wm_minimize_window(window_t *win);
void wm_restore_window(window_t *win);
void wm_toggle_maximize(window_t *win);
bool wm_resize_window(window_t *win, int w, int h);
void wm_set_reserved_bottom(int pixels);
int wm_get_reserved_bottom(void);
bool wm_chrome_handle_event(window_t *win, int rel_x, int rel_y, bool pressed);

void wm_mark_dirty(void);
bool wm_is_dirty(void);

#ifdef __cplusplus
}
#endif
