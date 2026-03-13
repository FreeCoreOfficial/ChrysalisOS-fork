#include "file_manager_app.h"
extern "C" void serial(const char *fmt, ...);
extern "C" int exec_from_path(const char *path, char *const argv[]);
extern "C" int execve(const char *filename, char *const argv[],
                      char *const envp[]);
#include "../cmds/fat.h"
#include "../string.h"
#include "../terminal.h"
#include "../time/timer.h"
#include "../ui/flyui/draw.h"
#include "../ui/wm/wm.h"
#include "app_manager.h"
#include "image_viewer_app.h"
#include "notepad_app.h"

static window_t *fm_win = NULL;
static char current_path[256] = "/";
static fat_file_info_t files[32];
static int file_count = 0;
static int selected_idx = -1;
static int last_click_idx = -1;
static uint64_t last_click_ms = 0;

static void draw_fm(surface_t *s);

static void fm_close(window_t *win) {
  (void)win;
  if (fm_win) {
    wm_destroy_window(fm_win);
    app_unregister(fm_win);
    fm_win = NULL;
    wm_mark_dirty();
  }
}

static void fm_on_resize(window_t *win) {
  if (!win || !win->surface)
    return;
  draw_fm(win->surface);
  wm_mark_dirty();
}

static void append_path(char *dst, size_t cap, const char *src) {
  size_t len = strlen(dst);
  size_t i = 0;
  while (src[i] && (len + 1) < cap) {
    dst[len++] = src[i++];
  }
  dst[len] = 0;
}

static char lower_ascii(char c) {
  if (c >= 'A' && c <= 'Z')
    return (char)(c + 32);
  return c;
}

static bool has_ext_ci(const char *name, const char *ext) {
  if (!name || !ext)
    return false;
  size_t nlen = strlen(name);
  size_t elen = strlen(ext);
  if (nlen < elen)
    return false;
  const char *p = name + (nlen - elen);
  for (size_t i = 0; i < elen; i++) {
    if (lower_ascii(p[i]) != lower_ascii(ext[i]))
      return false;
  }
  return true;
}

static void fm_go_up(void) {
  if (strcmp(current_path, "/") == 0)
    return;
  int len = strlen(current_path);
  while (len > 0 && current_path[len - 1] != '/')
    len--;
  if (len == 0)
    strcpy(current_path, "/");
  else {
    current_path[len - 1] = 0;
    if (strlen(current_path) == 0)
      strcpy(current_path, "/");
  }
}

static void build_breadcrumb(char *out, size_t cap) {
  if (!out || cap == 0)
    return;
  out[0] = 0;

  if (strcmp(current_path, "/") == 0) {
    strncpy(out, "/", cap);
    out[cap - 1] = 0;
    return;
  }

  append_path(out, cap, "/");
  char tmp[256];
  strncpy(tmp, current_path, sizeof(tmp));
  tmp[sizeof(tmp) - 1] = 0;

  char *p = tmp;
  if (*p == '/')
    p++;
  while (*p) {
    char *seg = p;
    while (*p && *p != '/')
      p++;
    char saved = *p;
    *p = 0;

    append_path(out, cap, " > ");
    append_path(out, cap, seg);

    if (saved == 0)
      break;
    *p = saved;
    p++;
  }
}

static void build_path(char *out, size_t cap, const char *name) {
  if (strcmp(current_path, "/") == 0) {
    strncpy(out, "/", cap);
    out[cap - 1] = 0;
    append_path(out, cap, name);
  } else {
    strncpy(out, current_path, cap);
    out[cap - 1] = 0;
    if (out[strlen(out) - 1] != '/')
      append_path(out, cap, "/");
    append_path(out, cap, name);
  }
}

static void create_new_file(void) {
  fat_automount();
  char name[256];
  char path[256];
  int idx = 0;
  while (idx < 100) {
    if (idx == 0)
      strcpy(name, "new.txt");
    else {
      strcpy(name, "new");
      char buf[8];
      itoa_dec(buf, idx);
      strcat(name, buf);
      strcat(name, ".txt");
    }
    name[255] = 0;
    build_path(path, sizeof(path), name);
    if (fat32_get_file_size(path) < 0) {
      fat32_create_file(path, "", 0);
      break;
    }
    idx++;
  }
}

static void create_new_folder(void) {
  fat_automount();
  char name[256];
  char path[256];
  int idx = 0;
  while (idx < 100) {
    if (idx == 0)
      strcpy(name, "new_folder");
    else {
      strcpy(name, "new_folder");
      char buf[8];
      itoa_dec(buf, idx);
      strcat(name, buf);
    }
    name[255] = 0;
    build_path(path, sizeof(path), name);
    if (!fat32_directory_exists(path)) {
      fat32_create_directory(path);
      break;
    }
    idx++;
  }
}

static void refresh_files() {
  fat_automount();
  file_count = fat32_read_directory(current_path, files, 32);
}

static void draw_fm(surface_t *s) {
  fly_draw_rect_fill(s, 0, 0, s->width, s->height, 0xFFFFFFFF);
  fly_draw_rect_fill(s, 0, 0, s->width, 24, 0xFF000080);
  fly_draw_text(s, 5, 4, "File Manager", 0xFFFFFFFF);
  fly_draw_rect_fill(s, s->width - 20, 4, 16, 16, 0xFFC0C0C0);
  fly_draw_text(s, s->width - 16, 4, "X", 0xFF000000);

  /* Path Bar */
  fly_draw_rect_fill(s, 5, 28, 18, 18, 0xFFC0C0C0);
  fly_draw_rect_outline(s, 5, 28, 18, 18, 0xFF000000);
  fly_draw_text(s, 9, 30, "<", 0xFF000000);

  fly_draw_text(s, 28, 30, "Path:", 0xFF000000);
  fly_draw_text(s, 70, 30, current_path, 0xFF000000);

  char breadcrumb[256];
  build_breadcrumb(breadcrumb, sizeof(breadcrumb));
  fly_draw_text(s, 28, 44, breadcrumb, 0xFF404040);
  fly_draw_rect_outline(s, 0, 58, s->width, 1, 0xFF000000);

  /* New File / New Folder Buttons */
  fly_draw_rect_fill(s, s->width - 140, 28, 60, 18, 0xFFC0C0C0);
  fly_draw_rect_outline(s, s->width - 140, 28, 60, 18, 0xFF000000);
  fly_draw_text(s, s->width - 135, 30, "New", 0xFF000000);

  fly_draw_rect_fill(s, s->width - 75, 28, 70, 18, 0xFFC0C0C0);
  fly_draw_rect_outline(s, s->width - 75, 28, 70, 18, 0xFF000000);
  fly_draw_text(s, s->width - 70, 30, "Folder", 0xFF000000);

  /* File List */
  int y = 66;
  for (int i = 0; i < file_count; i++) {
    uint32_t bg = (i == selected_idx) ? 0xFF000080 : 0xFFFFFFFF;
    uint32_t fg = (i == selected_idx) ? 0xFFFFFFFF : 0xFF000000;

    fly_draw_rect_fill(s, 5, y, s->width - 10, 18, bg);

    char label[256];
    if (files[i].is_dir) {
      /* Draw Folder Icon (Yellow Rect) */
      fly_draw_rect_fill(s, 10, y + 2, 12, 12, 0xFFFFFF00);
      strcpy(label, "      "); /* Space for icon */
      strcat(label, files[i].name);
    } else {
      strcpy(label, files[i].name);
    }
    fly_draw_text(s, 25, y + 2, label, fg);
    y += 20;
  }

  /* Selected item full name (status line) */
  if (selected_idx >= 0 && selected_idx < file_count) {
    fly_draw_rect_fill(s, 0, s->height - 18, s->width, 18, 0xFFF0F0F0);
    fly_draw_rect_outline(s, 0, s->height - 18, s->width, 1, 0xFF000000);
    fly_draw_text(s, 5, s->height - 16, files[selected_idx].name, 0xFF000000);
  }
}

void file_manager_app_create(void) {
  if (fm_win) {
    wm_restore_window(fm_win);
    wm_focus_window(fm_win);
    return;
  }
  surface_t *s = surface_create(300, 400);
  fm_win = wm_create_window(s, 50, 50);
  if (fm_win) {
    wm_set_title(fm_win, "File Manager (Debug)");
    wm_set_on_close(fm_win, fm_close);
    wm_set_on_resize(fm_win, fm_on_resize);
  }
  refresh_files();
  draw_fm(s);
  app_register("File Manager", fm_win);
}

void file_manager_app_open_path(const char *path) {
  if (!path || !path[0])
    return;

  if (!fm_win) {
    file_manager_app_create();
  } else {
    wm_restore_window(fm_win);
    wm_focus_window(fm_win);
  }

  fat_automount();
  if (fat32_directory_exists(path)) {
    strncpy(current_path, path, sizeof(current_path) - 1);
    current_path[sizeof(current_path) - 1] = 0;
  } else {
    /* If a file path was provided, navigate to its parent */
    strncpy(current_path, path, sizeof(current_path) - 1);
    current_path[sizeof(current_path) - 1] = 0;
    char *last = strrchr(current_path, '/');
    if (last && last != current_path) {
      *last = 0;
    } else {
      strcpy(current_path, "/");
    }
  }

  refresh_files();
  selected_idx = -1;
  if (fm_win && fm_win->surface) {
    draw_fm(fm_win->surface);
    wm_mark_dirty();
  }
}

bool file_manager_app_handle_event(input_event_t *ev) {
  if (!fm_win)
    return false;
  if (ev->type == INPUT_MOUSE_CLICK && ev->pressed) {
    int lx = ev->mouse_x - fm_win->x;
    int ly = ev->mouse_y - fm_win->y;

    /* Close */
    if (lx >= fm_win->w - 20 && ly < 24) {
      fm_close(fm_win);
      return true;
    }

    /* Back Button */
    if (lx >= 5 && lx <= 23 && ly >= 28 && ly <= 46) {
      fm_go_up();
      refresh_files();
      selected_idx = -1;
      draw_fm(fm_win->surface);
      wm_mark_dirty();
      return true;
    }

    /* New File / New Folder */
    if (ly >= 28 && ly <= 46) {
      int w = fm_win->surface->width;
      if (lx >= w - 140 && lx <= w - 80) {
        create_new_file();
        refresh_files();
        draw_fm(fm_win->surface);
        wm_mark_dirty();
        return true;
      }
      if (lx >= w - 75 && lx <= w - 5) {
        create_new_folder();
        refresh_files();
        draw_fm(fm_win->surface);
        wm_mark_dirty();
        return true;
      }
    }

    /* File Click */
    if (ly >= 66) {
      int idx = (ly - 66) / 20;
      if (idx >= 0 && idx < file_count) {
        uint64_t now_ms = timer_uptime_ms();
        bool is_double =
            (idx == last_click_idx) && (now_ms - last_click_ms) <= 350;
        last_click_idx = idx;
        last_click_ms = now_ms;

        if (is_double) {
          /* Open logic */
          if (files[idx].is_dir) {
            if (strcmp(files[idx].name, ".") == 0) { /* no-op */
            } else if (strcmp(files[idx].name, "..") == 0) {
              /* Go Up */
              fm_go_up();
            } else {
              /* Go Down */
              if (strcmp(current_path, "/") != 0)
                strcat(current_path, "/");
              strcat(current_path, files[idx].name);
            }
            refresh_files();
            selected_idx = -1;
          } else {
            char fullpath[256];
            if (strcmp(current_path, "/") == 0) {
              strncpy(fullpath, "/", sizeof(fullpath));
              fullpath[sizeof(fullpath) - 1] = 0;
              append_path(fullpath, sizeof(fullpath), files[idx].name);
            } else {
              strncpy(fullpath, current_path, sizeof(fullpath));
              fullpath[sizeof(fullpath) - 1] = 0;
              if (fullpath[strlen(fullpath) - 1] != '/') {
                append_path(fullpath, sizeof(fullpath), "/");
              }
              append_path(fullpath, sizeof(fullpath), files[idx].name);
            }
            if (has_ext_ci(files[idx].name, ".petal")) {
              exec_from_path(fullpath, NULL);
            } else if (has_ext_ci(files[idx].name, ".bmp")) {
              char *viewer_argv[] = {(char *)"/system/apps/image-viewer.petal",
                                     fullpath, nullptr};
              int rc = execve("/system/apps/image-viewer.petal", viewer_argv,
                              nullptr);
              if (rc < 0) {
                terminal_printf("[FM] Failed to launch image-viewer for '%s'\n",
                                fullpath);
                serial("[FM] Failed to launch image-viewer for '%s'\n",
                       fullpath);
              }
            } else {
              notepad_app_open(fullpath);
            }
          }
          last_click_idx = -1;
          last_click_ms = 0;
        } else {
          selected_idx = idx;
        }
        draw_fm(fm_win->surface);
        wm_mark_dirty();
      }
    }
    return true;
  }
  return false;
}

window_t *file_manager_app_get_window(void) { return fm_win; }
