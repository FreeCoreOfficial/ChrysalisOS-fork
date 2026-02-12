#include "run_dialog_app.h"
#include "../proc/exec.h"
#include "../shell/shell.h"
#include "../string.h"
#include "../ui/flyui/draw.h"
#include "../ui/flyui/theme.h"
#include "../ui/wm/wm.h"
#include "apps.h"
#include "minesweeper_app.h"

static window_t *run_win = NULL;
static window_t *secret_win = NULL;
static char cmd_buf[64] = "";

static void draw_run(surface_t *s);
static void draw_secret(surface_t *s);
static void show_secret_popup(const char *title, const char *line1,
                              const char *line2);
static const char *normalize_run_command(const char *in, char *out, size_t cap);

static void run_close(window_t *win) {
  (void)win;
  if (run_win) {
    wm_destroy_window(run_win);
    run_win = NULL;
    wm_mark_dirty();
  }
}

static void run_on_resize(window_t *win) {
  if (!win || !win->surface)
    return;
  draw_run(win->surface);
  wm_mark_dirty();
}

static void secret_close(window_t *win) {
  (void)win;
  if (secret_win) {
    wm_destroy_window(secret_win);
    secret_win = NULL;
    wm_mark_dirty();
  }
}

static void secret_on_resize(window_t *win) {
  if (!win || !win->surface)
    return;
  draw_secret(win->surface);
  wm_mark_dirty();
}

static void draw_run(surface_t *s) {
  fly_theme_t *th = theme_get();
  fly_draw_rect_fill(s, 0, 0, 300, 120, th->win_bg);
  uint32_t top = th->win_title_active_bg;
  uint32_t bot = (th->win_title_active_bg & 0xFEFEFEFE) - 0x00121212;
  fly_draw_rect_vgradient(s, 0, 0, 300, 24, top, bot);
  fly_draw_text(s, 8, 4, "Run", th->win_title_active_fg);
  fly_draw_rect_fill(s, 280, 4, 16, 16, 0xFFC44747);
  fly_draw_rect_outline(s, 280, 4, 16, 16, th->color_lo_2);
  fly_draw_text(s, 284, 4, "X", 0xFFFFFFFF);

  fly_draw_text(s, 10, 35,
                "Type the name of a program to open:", th->color_text);

  /* Input Box */
  fly_draw_rect_fill(s, 10, 55, 280, 20, 0xFFFFFFFF);
  fly_draw_rect_outline(s, 10, 55, 280, 20, th->color_lo_2);
  fly_draw_text(s, 15, 57, cmd_buf, th->color_text);

  /* OK Button */
  fly_draw_rect_vgradient(s, 200, 85, 80, 25, 0xFFF7F9FC, 0xFFD7DEE7);
  fly_draw_rect_outline(s, 200, 85, 80, 25, th->color_lo_2);
  fly_draw_text(s, 225, 90, "Run", th->color_text);
}

static char secret_title[48];
static char secret_line1[80];
static char secret_line2[80];

static void draw_secret(surface_t *s) {
  fly_theme_t *th = theme_get();
  fly_draw_rect_fill(s, 0, 0, 320, 120, th->win_bg);
  uint32_t top = th->win_title_active_bg;
  uint32_t bot = (th->win_title_active_bg & 0xFEFEFEFE) - 0x00121212;
  fly_draw_rect_vgradient(s, 0, 0, 320, 24, top, bot);
  fly_draw_text(s, 8, 4, secret_title, th->win_title_active_fg);
  fly_draw_rect_fill(s, 296, 4, 16, 16, 0xFFC44747);
  fly_draw_rect_outline(s, 296, 4, 16, 16, th->color_lo_2);
  fly_draw_text(s, 300, 4, "X", 0xFFFFFFFF);

  fly_draw_text(s, 12, 42, secret_line1, th->color_text);
  if (secret_line2[0]) {
    fly_draw_text(s, 12, 62, secret_line2, th->color_text);
  }
}

static void show_secret_popup(const char *title, const char *line1,
                              const char *line2) {
  if (secret_win) {
    wm_restore_window(secret_win);
    wm_focus_window(secret_win);
  } else {
    surface_t *s = surface_create(320, 120);
    secret_win = wm_create_window(s, 160, 220);
    if (secret_win) {
      wm_set_title(secret_win, "Secret");
      wm_set_on_close(secret_win, secret_close);
      wm_set_on_resize(secret_win, secret_on_resize);
    }
  }

  strncpy(secret_title, title, sizeof(secret_title));
  secret_title[sizeof(secret_title) - 1] = 0;
  strncpy(secret_line1, line1, sizeof(secret_line1));
  secret_line1[sizeof(secret_line1) - 1] = 0;
  if (line2) {
    strncpy(secret_line2, line2, sizeof(secret_line2));
    secret_line2[sizeof(secret_line2) - 1] = 0;
  } else {
    secret_line2[0] = 0;
  }

  if (secret_win && secret_win->surface) {
    draw_secret(secret_win->surface);
    wm_mark_dirty();
  }
}

static const char *normalize_run_command(const char *in, char *out,
                                         size_t cap) {
  if (!out || cap == 0)
    return "";
  out[0] = 0;
  if (!in)
    return out;

  /* Skip leading spaces */
  while (*in == ' ' || *in == '\t')
    in++;

  /* Handle -execute prefix (Run dialog only) */
  if (strncmp(in, "-execute", 8) == 0) {
    in += 8;
    while (*in == ' ' || *in == '\t')
      in++;
    if (*in == '(') {
      in++;
      while (*in == ' ' || *in == '\t')
        in++;
    }
    if (*in == '"' || *in == '\'') {
      char quote = *in++;
      size_t i = 0;
      while (*in && *in != quote && i + 1 < cap) {
        out[i++] = *in++;
      }
      out[i] = 0;
      return out;
    }
    /* Copy until ')' or end */
    size_t i = 0;
    while (*in && *in != ')' && i + 1 < cap) {
      out[i++] = *in++;
    }
    out[i] = 0;
    return out;
  }

  /* Default: trim and copy */
  size_t i = 0;
  while (*in && i + 1 < cap) {
    out[i++] = *in++;
  }
  out[i] = 0;
  return out;
}

static void execute_run() {
  char norm[64];
  const char *cmd = normalize_run_command(cmd_buf, norm, sizeof(norm));

  if (strcmp(cmd, "calc") == 0)
    calculator_app_create();
  else if (strcmp(cmd, "notepad") == 0)
    notepad_app_create();
  else if (strcmp(cmd, "clock") == 0)
    clock_app_create();
  else if (strcmp(cmd, "calendar") == 0)
    calendar_app_create();
  else if (strcmp(cmd, "files") == 0)
    file_manager_app_create();
  else if (strcmp(cmd, "info") == 0)
    sysinfo_app_create();
  else if (strcmp(cmd, "paint") == 0)
    paint_app_create();
  else if (strcmp(cmd, "demo") == 0)
    demo3d_app_create();
  else if (strcmp(cmd, "terminal") == 0)
    shell_create_window();
  else if (strncmp(cmd, "view ", 5) == 0)
    image_viewer_app_create(cmd + 5);
  else if (strcmp(cmd, "rickroll") == 0) {
    show_secret_popup("Never Gonna Give You Up", "Never gonna give you up,",
                      "never gonna let you down.");
  } else if (strcmp(cmd, "yaai") == 0) {
    show_secret_popup("You are an idiot", ":-) You are an idiot! :-)",
                      "Ha ha ha ha ha!");
  } else if (strcmp(cmd, "minesweeper_god=1") == 0) {
    minesweeper_god_app_create();
  } else {
    /* Check if it's a .petal file on disk */
    int len = strlen(cmd);
    if (len > 6 && strcmp(cmd + len - 6, ".petal") == 0) {
      /* Try to execute using kernel execve */
      /* We pass nullptr for argv[1] etc for now */
      char *args[] = {(char *)cmd, nullptr};
      execve(cmd, args, nullptr);
    } else if (cmd[0] == '/') {
      /* Full path provided, check if it's an app */
      char *args[] = {(char *)cmd, nullptr};
      execve(cmd, args, nullptr);
    }
  }

  wm_destroy_window(run_win);
  run_win = NULL;
  wm_mark_dirty();
}

void run_dialog_app_create(void) {
  if (run_win) {
    wm_restore_window(run_win);
    wm_focus_window(run_win);
    return;
  }
  surface_t *s = surface_create(300, 120);
  run_win = wm_create_window(s, 100, 300);
  if (run_win) {
    wm_set_title(run_win, "Run");
    wm_set_on_close(run_win, run_close);
    wm_set_on_resize(run_win, run_on_resize);
  }
  cmd_buf[0] = 0;
  draw_run(s);
}

bool run_dialog_app_handle_event(input_event_t *ev) {
  if (!run_win)
    return false;
  if (ev->type == INPUT_MOUSE_CLICK && ev->pressed) {
    int lx = ev->mouse_x - run_win->x;
    int ly = ev->mouse_y - run_win->y;

    /* Close */
    if (lx >= 280 && ly < 24) {
      run_close(run_win);
      return true;
    }
    /* Run Button */
    if (lx >= 200 && lx <= 280 && ly >= 85 && ly <= 110) {
      execute_run();
      return true;
    }
  }
  return false;
}

bool run_dialog_secret_handle_event(input_event_t *ev) {
  if (!secret_win)
    return false;
  if (ev->type == INPUT_MOUSE_CLICK && ev->pressed) {
    int lx = ev->mouse_x - secret_win->x;
    int ly = ev->mouse_y - secret_win->y;
    if (lx >= 296 && ly < 24) {
      secret_close(secret_win);
      return true;
    }
  }
  return false;
}

void run_dialog_app_handle_key(char c) {
  if (!run_win)
    return;
  int len = strlen(cmd_buf);
  if (c == '\b') {
    if (len > 0)
      cmd_buf[len - 1] = 0;
  } else if (c == '\n') {
    execute_run();
    return;
  } else if (c >= 32 && c <= 126) {
    if (len < 60) {
      cmd_buf[len] = c;
      cmd_buf[len + 1] = 0;
    }
  }
  draw_run(run_win->surface);
  wm_mark_dirty();
}

window_t *run_dialog_app_get_window(void) { return run_win; }
window_t *run_dialog_secret_get_window(void) { return secret_win; }
