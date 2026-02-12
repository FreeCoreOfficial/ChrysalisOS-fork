#include "shell.h"
#include "../cmds/cd.h"
#include "../cmds/registry.h"
#include "../colors/cl.h"
#include "../drivers/serial.h"
#include "../events/event_queue.h"
#include "../mem/kmalloc.h"
#include "../proc/exec.h"
#include "../string.h"
#include "../terminal.h"
#include "../ui/flyui/draw.h"
#include "../ui/flyui/theme.h"
#include "../ui/wm/wm.h"
#include "../user/user.h"
#include "../video/surface.h"

/* Configuration */
#define SHELL_BUF_SIZE 256
#define SHELL_MAX_ARGS 32
#define HISTORY_SIZE 16
#define PIPE_BUF_SIZE 4096
#define MAX_SHELL_CONTEXTS 8

/* Key definitions (ASCII control codes) */
#define KEY_ENTER '\n'
#define KEY_BACKSPACE '\b'
#define KEY_TAB '\t'
#define KEY_UP 0x80
#define KEY_DOWN 0x81
#define KEY_LEFT 0x82
#define KEY_RIGHT 0x83
#define KEY_CTRL_P 16 /* Up (Previous) */
#define KEY_CTRL_N 14 /* Down (Next) */

/* Shell Context Structure */
typedef struct {
  char line[SHELL_BUF_SIZE];
  int line_len;
  int cursor;
  int last_rendered_len;
  char history[HISTORY_SIZE][SHELL_BUF_SIZE];
  int hist_head;
  int hist_count;
  int hist_pos;

  /* Login State */
  int state; // 0=NORMAL, 1=LOGIN_USER, 2=LOGIN_PASS
  char temp_user[32];
} shell_ctx_t;

static shell_ctx_t contexts[MAX_SHELL_CONTEXTS];
static int active_ctx_id = 0;

/* Import serial logging */
extern "C" void serial(const char *fmt, ...);

/* --- Helpers --- */

static void shell_render_line() {
  shell_ctx_t *ctx = &contexts[active_ctx_id];

  /* Optimization: Use a local buffer to avoid multiple terminal calls */
  char buf[SHELL_BUF_SIZE + 64];
  buf[0] = '\r';
  buf[1] = 0;
  (void)buf; /* Planned optimization */

  /* Render prompt and current line content */
  terminal_putchar('\r');
  shell_prompt();

  if (ctx->state == 2) {
    /* Password Masking */
    for (int i = 0; i < ctx->line_len; i++)
      terminal_putchar('*');
  } else {
    terminal_writestring(ctx->line);
  }

  /* Clear trailing garbage */
  if (ctx->last_rendered_len > ctx->line_len) {
    int diff = ctx->last_rendered_len - ctx->line_len;
    for (int i = 0; i < diff; i++)
      terminal_putchar(' ');
    for (int i = 0; i < diff; i++)
      terminal_putchar('\b');
  }

  /* Move cursor */
  for (int i = ctx->line_len; i > ctx->cursor; i--) {
    terminal_putchar('\b');
  }

  ctx->last_rendered_len = ctx->line_len;
  wm_mark_dirty();
}

static void shell_history_add(const char *cmd) {
  shell_ctx_t *ctx = &contexts[active_ctx_id];
  if (!cmd || !*cmd)
    return;

  /* Don't add duplicates of the immediate last command */
  if (ctx->hist_count > 0) {
    int last_idx = (ctx->hist_head - 1 + HISTORY_SIZE) % HISTORY_SIZE;
    if (strcmp(ctx->history[last_idx], cmd) == 0)
      return;
  }

  /* Add to ring buffer */
  strcpy(ctx->history[ctx->hist_head], cmd);
  ctx->hist_head = (ctx->hist_head + 1) % HISTORY_SIZE;
  if (ctx->hist_count < HISTORY_SIZE)
    ctx->hist_count++;

  serial("[SHELL] History add: %s\n", cmd);
}

static void shell_history_nav(int dir) {
  shell_ctx_t *ctx = &contexts[active_ctx_id];
  /* dir: 1 for UP (older), -1 for DOWN (newer) */
  if (ctx->hist_count == 0)
    return;

  if (ctx->hist_pos == -1) {
    /* Currently editing new line */
    if (dir == 1) {
      ctx->hist_pos = 0; /* Go to latest history */
    } else {
      return;
    }
  } else {
    ctx->hist_pos += dir;
  }

  /* Bounds check */
  if (ctx->hist_pos < -1)
    ctx->hist_pos = -1;
  if (ctx->hist_pos >= ctx->hist_count)
    ctx->hist_pos = ctx->hist_count - 1;

  /* Load line */
  if (ctx->hist_pos == -1) {
    ctx->line[0] = 0;
    ctx->line_len = 0;
    ctx->cursor = 0;
  } else {
    /* Calculate actual index in ring buffer (0 = latest) */
    int idx =
        (ctx->hist_head - 1 - ctx->hist_pos + HISTORY_SIZE * 2) % HISTORY_SIZE;
    strcpy(ctx->line, ctx->history[idx]);
    ctx->line_len = strlen(ctx->line);
    ctx->cursor = ctx->line_len;
  }

  shell_render_line();
  serial("[SHELL] History nav: pos=%d\n", ctx->hist_pos);
}

static void shell_exec_single(char *cmd_str) {
  /* Parse arguments */
  char *argv[SHELL_MAX_ARGS];
  int argc = 0;

  char *p = cmd_str;
  while (*p && argc < SHELL_MAX_ARGS) {
    /* Skip whitespace */
    while (*p == ' ' || *p == '\t')
      p++;
    if (!*p)
      break;

    /* Start of token */
    if (*p == '"') {
      /* Quoted string */
      p++; // Skip opening quote
      argv[argc++] = p;
      while (*p && *p != '"') {
        if (*p == '\\' && *(p + 1)) {
          /* Handle escape: skip backslash, keep next char */
          /* In-place shift would be better, but for now we just skip \ */
          p++;
        }
        p++;
      }
      if (*p == '"')
        *p++ = 0; // Terminate and skip closing quote
    } else {
      /* Normal word */
      argv[argc++] = p;
      while (*p && *p != ' ' && *p != '\t')
        p++;
      if (*p)
        *p++ = 0; // Terminate
    }
  }

  if (argc > 0) {
    serial("[SHELL] Exec: argc=%d cmd='%s'\n", argc, argv[0]);

    /* Check for Chrysalis Script (.csr / .chs) */
    int len = strlen(argv[0]);
    if (len > 4) {
      const char *ext = argv[0] + len - 4;
      if (strcmp(ext, ".csr") == 0 || strcmp(ext, ".chs") == 0) {
        serial("[SHELL] Detected script: %s\n", argv[0]);

        /* Construct argv for exec: ["cs", script_path, args...] */
        char *new_argv[SHELL_MAX_ARGS + 2];
        new_argv[0] = (char *)"cs";
        new_argv[1] = argv[0]; /* Script path */

        for (int i = 1; i < argc; i++) {
          new_argv[i + 1] = argv[i];
        }
        new_argv[argc + 1] = 0; /* Null terminate */

        /* Launch interpreter via exec */
        /* This maps to /bin/cs inside execve */
        execve("/bin/cs", new_argv, 0);
        return;
      }
    }

    /* Lookup command */
    int found = 0;
    for (int i = 0; i < command_count; i++) {
      if (strcmp(command_table[i].name, argv[0]) == 0) {
        command_table[i].func(argc, argv);
        found = 1;
        break;
      }
    }

    if (!found) {
      terminal_writestring("Unknown command: ");
      terminal_writestring(argv[0]);
      terminal_writestring("\n");
    }
  }
}

static void shell_exec_line() {
  shell_ctx_t *ctx = &contexts[active_ctx_id];
  terminal_putchar('\n');

  if (ctx->state == 1) {
    /* Got Username */
    if (ctx->line_len > 0) {
      strncpy(ctx->temp_user, ctx->line, 31);
      ctx->temp_user[31] = 0;
      ctx->state = 2; /* Move to Password */
    }
    ctx->line[0] = 0;
    ctx->line_len = 0;
    ctx->cursor = 0;
    shell_prompt();
    return;
  }

  if (ctx->state == 2) {
    /* Got Password */
    /* Try login */
    if (user_switch(ctx->temp_user, ctx->line) == 0) {
      ctx->state = 0; /* Success */
      terminal_printf("Welcome, %s!\n", ctx->temp_user);
    } else {
      terminal_printf("Login incorrect.\n");
      ctx->state = 1; /* Retry username */
    }
    ctx->line[0] = 0;
    ctx->line_len = 0;
    ctx->cursor = 0;
    shell_prompt();
    return;
  }

  if (ctx->line_len == 0) {
    shell_prompt();
    return;
  }

  /* Add to history */
  shell_history_add(ctx->line);
  ctx->hist_pos = -1; // Reset history navigation

  /* Pipe buffers */
  char *buf_in = 0;
  size_t len_in = 0;

  char *buf_out = 0;
  size_t len_out = 0;

  /* Split by pipe '|' */
  char *p = ctx->line;
  char *cmd_start = p;

  while (*p) {
    /* Find next pipe or end of string */
    char *pipe_ptr = p;
    while (*pipe_ptr && *pipe_ptr != '|')
      pipe_ptr++;

    bool is_last = (*pipe_ptr == 0);
    if (!is_last) {
      *pipe_ptr = 0; /* Terminate current command string */
    }

    /* Prepare Output Buffer if not last */
    if (!is_last) {
      buf_out = (char *)kmalloc(PIPE_BUF_SIZE);
      if (buf_out) {
        serial("[SHELL] Pipe created (buf=0x%x)\n", buf_out);
        terminal_start_capture(buf_out, PIPE_BUF_SIZE, &len_out);
      } else {
        serial("[SHELL] Pipe alloc failed!\n");
      }
    } else {
      terminal_end_capture();
    }

    /* Setup Input from previous pipe */
    if (buf_in) {
      terminal_set_input(buf_in, len_in);
    } else {
      terminal_set_input(0, 0);
    }

    /* Execute Command */
    shell_exec_single(cmd_start);

    /* Cleanup Input */
    if (buf_in) {
      kfree(buf_in);
      buf_in = 0;
      len_in = 0;
    }

    /* Prepare for next command */
    if (!is_last) {
      terminal_end_capture();
      if (buf_out) {
        serial("[SHELL] Pipe write: %d bytes\n", len_out);
        buf_in = buf_out; /* Output becomes input for next */
        len_in = len_out;
        buf_out = 0;
      }
      cmd_start = pipe_ptr + 1;
      p = cmd_start;
    } else {
      break;
    }
  }

  /* Ensure cleanup */
  terminal_set_input(0, 0);
  terminal_end_capture();
  if (buf_in)
    kfree(buf_in);
  if (buf_out)
    kfree(buf_out);

  /* Reset line */
  ctx->line[0] = 0;
  ctx->line_len = 0;
  ctx->cursor = 0;
  ctx->last_rendered_len = 0;

  shell_prompt();
}

static void shell_autocomplete() {
  shell_ctx_t *ctx = &contexts[active_ctx_id];
  /* Find word start */
  int start = ctx->cursor;
  while (start > 0 && ctx->line[start - 1] != ' ')
    start--;

  int word_len = ctx->cursor - start;
  if (word_len == 0)
    return;

  char prefix[64];
  int i;
  for (i = 0; i < word_len && i < 63; i++)
    prefix[i] = ctx->line[start + i];
  prefix[i] = 0;

  serial("[SHELL] Autocomplete prefix: '%s'\n", prefix);

  int matches = 0;
  const char *last_match = 0;

  for (int j = 0; j < command_count; j++) {
    if (strncmp(command_table[j].name, prefix, word_len) == 0) {
      matches++;
      last_match = command_table[j].name;
    }
  }

  if (matches == 1) {
    /* Complete it */
    const char *rest = last_match + word_len;
    while (*rest) {
      if (ctx->line_len < SHELL_BUF_SIZE - 1) {
        ctx->line[ctx->line_len++] = *rest;
        ctx->line[ctx->line_len] = 0;
        ctx->cursor++;
      }
      rest++;
    }
    /* Add space */
    if (ctx->line_len < SHELL_BUF_SIZE - 1) {
      ctx->line[ctx->line_len++] = ' ';
      ctx->line[ctx->line_len] = 0;
      ctx->cursor++;
    }
    shell_render_line();
  } else if (matches > 1) {
    /* Show candidates */
    terminal_putchar('\n');
    for (int j = 0; j < command_count; j++) {
      if (strncmp(command_table[j].name, prefix, word_len) == 0) {
        terminal_writestring(command_table[j].name);
        terminal_writestring(" ");
      }
    }
    terminal_putchar('\n');
    shell_render_line();
  }
}

/* --- Public API --- */

static window_t *shell_win = NULL;

static void shell_on_resize(window_t *win) {
  if (!win || !win->surface)
    return;
  int win_w = win->w;
  int win_h = win->h;
  terminal_set_surface(win->surface);
  terminal_set_rect(0, 25, win_w, win_h - 25);
  terminal_clear();
  wm_mark_dirty();
}

static void shell_on_close(window_t *win) {
  (void)win;
  shell_destroy_window();
}

static void fly_draw_line(surface_t *surf, int x0, int y0, int x1, int y1,
                          uint32_t color) {
  int dx = (x1 > x0) ? (x1 - x0) : (x0 - x1);
  int sx = (x0 < x1) ? 1 : -1;
  int dy = (y1 > y0) ? -(y1 - y0) : -(y0 - y1);
  int sy = (y0 < y1) ? 1 : -1;
  int err = dx + dy;
  int e2;

  for (;;) {
    if (x0 >= 0 && x0 < (int)surf->width && y0 >= 0 && y0 < (int)surf->height) {
      surf->pixels[y0 * surf->width + x0] = color;
    }
    if (x0 == x1 && y0 == y1)
      break;
    e2 = 2 * err;
    if (e2 >= dy) {
      err += dy;
      x0 += sx;
    }
    if (e2 <= dx) {
      err += dx;
      y0 += sy;
    }
  }
}

void shell_init() {
  /* Text Mode Init Only */
  shell_init_context(0);
  terminal_clear();
  serial("[SHELL] Initialized (Text Mode).\n");
  shell_prompt();
}

void shell_create_window() {
  fly_theme_t *th = theme_get();
  int win_w = 640;
  int win_h = 400 + 25; /* +24 for title bar + 1 border */
  surface_t *s = surface_create(win_w, win_h);
  if (s) {
    surface_clear(s, th->win_bg); /* Window background */

    /* Draw Title Bar  */
    fly_draw_rect_fill(s, 0, 0, win_w, 24, th->win_title_active_bg);
    const char *title = "Chrysalis OS : Konsole";
    int title_len = strlen(title);
    int title_x = (win_w - (title_len * 8)) / 2;
    fly_draw_text(s, title_x, 4, title, th->win_title_active_fg);

    /* Close Button [X] */
    int bx = win_w - 20;
    int by = 4;
    fly_draw_rect_fill(s, bx, by, 16, 16, th->win_bg);
    fly_draw_rect_outline(s, bx, by, 16, 16, th->color_lo_2);
    fly_draw_line(s, bx, by, bx + 15, by, th->color_hi_1);
    fly_draw_line(s, bx, by, bx, by + 15, th->color_hi_1);
    fly_draw_text(s, bx + 4, by, "X", th->color_text);

    /* Border between title bar and content */
    fly_draw_rect_fill(s, 0, 24, win_w, 1, th->color_lo_1);

    shell_win = wm_create_window(s, 50, 50);
    if (shell_win) {
      wm_set_title(shell_win, "Konsole");
      wm_set_on_close(shell_win, shell_on_close);
      wm_set_on_resize(shell_win, shell_on_resize);
    }

    terminal_set_surface(s);
    terminal_set_rect(0, 25, win_w, 400);
    terminal_set_text_attr(cl_default());

    serial("[SHELL] Window created and terminal attached.\n");
  } else {
    serial("[SHELL] Error: Failed to create surface (OOM?)\n");
  }
}

void shell_destroy_window() {
  if (shell_win) {
    wm_destroy_window(shell_win);
    shell_win = NULL;
    terminal_set_surface(NULL);
    terminal_set_rendering(false);
  }
}

int shell_is_window_active() { return (shell_win != NULL); }

window_t *shell_get_window(void) { return shell_win; }

bool shell_handle_event(input_event_t *ev) {
  if (!shell_win)
    return false;

  if (ev->type == INPUT_MOUSE_CLICK && ev->pressed) {
    int lx = ev->mouse_x - shell_win->x;
    int ly = ev->mouse_y - shell_win->y;

    /* Close Button Rect: x=W-20, y=4, w=16, h=16 */
    if (lx >= shell_win->w - 20 && lx <= shell_win->w - 4 && ly >= 4 &&
        ly <= 20) {
      serial("[SHELL] Close button clicked.\n");
      shell_destroy_window();
      wm_mark_dirty();
      return true;
    }
  }
  return false;
}

void shell_init_context(int id) {
  if (id < 0 || id >= MAX_SHELL_CONTEXTS)
    return;
  shell_ctx_t *ctx = &contexts[id];
  ctx->line[0] = 0;
  ctx->line_len = 0;
  ctx->cursor = 0;
  ctx->hist_count = 0;
  ctx->hist_pos = -1;
  ctx->last_rendered_len = 0;

  if (user_get_current() == 0) {
    ctx->state = 1; /* Start with Login User */
  } else {
    ctx->state = 0;
  }
}

void shell_handle_char(char c) {
  shell_ctx_t *ctx = &contexts[active_ctx_id];
  if (c == KEY_ENTER) {
    shell_exec_line();
    return;
  }

  if (c == KEY_BACKSPACE) {
    if (ctx->cursor > 0) {
      /* Shift left */
      for (int i = ctx->cursor; i < ctx->line_len; i++) {
        ctx->line[i - 1] = ctx->line[i];
      }
      ctx->line_len--;
      ctx->cursor--;
      ctx->line[ctx->line_len] = 0;
      shell_render_line();
    }
    return;
  }

  if (c == KEY_TAB) {
    shell_autocomplete();
    return;
  }

  /* History Navigation (Ctrl-P/N as fallback for Up/Down) */
  if (c == KEY_CTRL_P) {
    shell_history_nav(1);
    return;
  }
  if (c == KEY_CTRL_N) {
    shell_history_nav(-1);
    return;
  }

  if ((uint8_t)c == KEY_LEFT) {
    if (ctx->cursor > 0) {
      ctx->cursor--;
      shell_render_line();
    }
    return;
  }
  if ((uint8_t)c == KEY_RIGHT) {
    if (ctx->cursor < ctx->line_len) {
      ctx->cursor++;
      shell_render_line();
    }
    return;
  }
  if ((uint8_t)c == KEY_UP) {
    shell_history_nav(1);
    return;
  }
  if ((uint8_t)c == KEY_DOWN) {
    shell_history_nav(-1);
    return;
  }

  /* Printable characters */
  if (c >= 32 && c <= 126) {
    if (ctx->line_len < SHELL_BUF_SIZE - 1) {
      bool is_at_end = (ctx->cursor == ctx->line_len);

      /* Insert at cursor */
      for (int i = ctx->line_len; i > ctx->cursor; i--) {
        ctx->line[i] = ctx->line[i - 1];
      }
      ctx->line[ctx->cursor] = c;
      ctx->line_len++;
      ctx->cursor++;
      ctx->line[ctx->line_len] = 0;

      if (is_at_end) {
        /* Optimization: Just print the char if we are at the end */
        terminal_putchar(c);
        ctx->last_rendered_len = ctx->line_len;
        wm_mark_dirty(); /* Ensure GUI refresh wakes up */
      } else {
        shell_render_line();
      }
    }
  }
}

void shell_reset_input(void) {
  shell_ctx_t *ctx = &contexts[active_ctx_id];
  ctx->line[0] = 0;
  ctx->line_len = 0;
  ctx->cursor = 0;
}

void shell_set_active_context(int id) {
  if (id < 0 || id >= MAX_SHELL_CONTEXTS)
    return;
  active_ctx_id = id;
}

void shell_prompt(void) {
  shell_ctx_t *ctx = &contexts[active_ctx_id];

  if (ctx->state == 1) {
    terminal_printf("login: ");
    return;
  }
  if (ctx->state == 2) {
    terminal_printf("password: ");
    return;
  }

  user_t *u = user_get_current();
  char cwd[64];
  cd_get_cwd(cwd, 64);
  if (u) {
    terminal_printf("%s@%s:%s$ ", u->name, u->hostname, cwd);
  } else {
    terminal_printf("guest@chrysalis:%s$ ", cwd);
  }
}

/* Legacy poll support */
void shell_poll_input() {
  event_t ev;
  while (event_pop(&ev) == 0) {
    if (ev.type == EVENT_KEY && ev.key.pressed && ev.key.ascii) {
      shell_handle_char(ev.key.ascii);
    }
  }
}
