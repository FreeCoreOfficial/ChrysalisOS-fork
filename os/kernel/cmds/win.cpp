#include "win.h"
#include "../apps/app_manager.h"
#include "../apps/apps.h"
#include "../apps/icons/icons.h"
#include "../drivers/keyboard.h"
#include "../ethernet/net.h"
#include "../ethernet/net_device.h"
#include "../hardware/apic.h"
#include "../input/input.h"
#include "../shell/shell.h"
#include "../storage/io_sched.h"
#include "../string.h"
#include "../terminal.h"
#include "../time/clock.h"
#include "../time/timer.h"
#include "../ui/flyui/draw.h"
#include "../ui/flyui/flyui.h"
#include "../ui/flyui/theme.h"
#include "../ui/flyui/widgets/widgets.h"
#include "../ui/wm/wm.h"
#include "../usb/usb_core.h"
#include "../user/user.h"
#include "../video/compositor.h"
#include "../video/gpu.h"
#include "shutdown.h"

extern "C" void serial(const char *fmt, ...);
extern "C" void yield();

/* Window control metrics */
#define WM_TITLEBAR_H 28
#define WM_BTN_SIZE 16
#define WM_BTN_PAD 4
#define WM_RESIZE_GRIP 12

/* Program Manager State */
static window_t *desktop_win = NULL;
static flyui_context_t *desktop_ctx = NULL;
static window_t *taskbar_win = NULL;
static flyui_context_t *taskbar_ctx = NULL;
static window_t *popup_win = NULL;
static flyui_context_t *popup_ctx = NULL;
static window_t *net_win = NULL;
static flyui_context_t *net_ctx = NULL;
static window_t *start_menu_win = NULL;
static flyui_context_t *start_menu_ctx = NULL;
static window_t *login_win = NULL;
static flyui_context_t *login_ctx = NULL;
static fly_widget_t *login_user_box = NULL;
static fly_widget_t *login_pass_box = NULL;
static fly_widget_t *login_msg_label = NULL;
static bool is_gui_running = false;
static int taskbar_last_min = -1;
static bool start_menu_just_toggled = false;

#define TASKBAR_H 36

/* Icon Button Logic */
/* Obsolete icon_btn_data_t removed */

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

static uint32_t shade_color(uint32_t c, int delta) {
  int a = (c >> 24) & 0xFF;
  int r = (c >> 16) & 0xFF;
  int g = (c >> 8) & 0xFF;
  int b = c & 0xFF;
  r += delta;
  g += delta;
  b += delta;
  if (r < 0)
    r = 0;
  if (r > 255)
    r = 255;
  if (g < 0)
    g = 0;
  if (g > 255)
    g = 255;
  if (b < 0)
    b = 0;
  if (b > 255)
    b = 255;
  return ((uint32_t)a << 24) | ((uint32_t)r << 16) | ((uint32_t)g << 8) |
         (uint32_t)b;
}

static void start_menu_bg_draw(fly_widget_t *w, surface_t *surf, int x, int y) {
  (void)x;
  (void)y;
  fly_theme_t *th = theme_get();
  uint32_t top = shade_color(th->win_bg, 12);
  uint32_t bot = shade_color(th->win_bg, -8);
  fly_draw_rect_vgradient(surf, 0, 0, w->w, w->h, top, bot);
  fly_draw_rect_outline(surf, 0, 0, w->w, w->h, th->color_lo_2);
}

static void start_menu_header_draw(fly_widget_t *w, surface_t *surf, int x,
                                   int y) {
  (void)x;
  (void)y;
  fly_theme_t *th = theme_get();
  uint32_t top = shade_color(th->win_title_active_bg, 18);
  uint32_t bot = shade_color(th->win_title_active_bg, -14);
  fly_draw_rect_vgradient(surf, 0, 0, w->w, w->h, top, bot);
  fly_draw_rect_fill(surf, 0, w->h - 1, w->w, 1,
                     shade_color(th->win_title_active_bg, -40));
}

static void start_menu_side_draw(fly_widget_t *w, surface_t *surf, int x,
                                 int y) {
  (void)x;
  (void)y;
  uint32_t top = 0xFF2E5D8A;
  uint32_t bot = 0xFF1D3652;
  fly_draw_rect_vgradient(surf, 0, 0, w->w, w->h, top, bot);
}

/* Button Handler */
/* Button Handler: Lansează Terminalul */
static void terminal_btn_click(fly_widget_t *w) {
  (void)w;
  if (start_menu_win) {
    wm_destroy_window(start_menu_win);
    start_menu_win = NULL;
    start_menu_ctx = NULL;
    wm_mark_dirty();
  }
  if (!shell_is_window_active()) {
    serial("[WIN] Launching Terminal Window...\n");
    shell_create_window();
    terminal_set_rendering(true);
    wm_mark_dirty();
  }
}

/* Button Handler: Lansează Ceasul */
static void clock_btn_click(fly_widget_t *w) {
  (void)w;
  if (start_menu_win) {
    wm_destroy_window(start_menu_win);
    start_menu_win = NULL;
    start_menu_ctx = NULL;
  }
  clock_app_create();
}

/* Button Handler: Lansează Calculatorul */
static void calc_btn_click(fly_widget_t *w) {
  (void)w;
  if (start_menu_win) {
    wm_destroy_window(start_menu_win);
    start_menu_win = NULL;
    start_menu_ctx = NULL;
  }
  calculator_app_create();
}

/* Button Handler: Lansează Notepad */
static void note_btn_click(fly_widget_t *w) {
  (void)w;
  if (start_menu_win) {
    wm_destroy_window(start_menu_win);
    start_menu_win = NULL;
    start_menu_ctx = NULL;
  }
  notepad_app_create();
}

/* Button Handler: Lansează Calendarul */
static void cal_btn_click(fly_widget_t *w) {
  (void)w;
  if (start_menu_win) {
    wm_destroy_window(start_menu_win);
    start_menu_win = NULL;
    start_menu_ctx = NULL;
  }
  calendar_app_create();
}

/* Button Handler: Lansează File Manager */
static void files_btn_click(fly_widget_t *w) {
  (void)w;
  if (start_menu_win) {
    wm_destroy_window(start_menu_win);
    start_menu_win = NULL;
    start_menu_ctx = NULL;
  }
  file_manager_app_create();
}

/* Button Handler: Lansează Image Viewer */
static void img_btn_click(fly_widget_t *w) {
  (void)w;
  if (start_menu_win) {
    wm_destroy_window(start_menu_win);
    start_menu_win = NULL;
    start_menu_ctx = NULL;
  }
  image_viewer_app_create(NULL);
}

/* Button Handler: Lansează SysInfo */
static void info_btn_click(fly_widget_t *w) {
  (void)w;
  if (start_menu_win) {
    wm_destroy_window(start_menu_win);
    start_menu_win = NULL;
    start_menu_ctx = NULL;
  }
  sysinfo_app_create();
}

/* Button Handler: Lansează Task Manager */
static void task_btn_click(fly_widget_t *w) {
  (void)w;
  if (start_menu_win) {
    wm_destroy_window(start_menu_win);
    start_menu_win = NULL;
    start_menu_ctx = NULL;
  }
  task_manager_app_create();
}

/* Button Handler: Lansează Paint */
static void paint_btn_click(fly_widget_t *w) {
  (void)w;
  if (start_menu_win) {
    wm_destroy_window(start_menu_win);
    start_menu_win = NULL;
    start_menu_ctx = NULL;
  }
  paint_app_create();
}

/* Button Handler: Lansează Demo 3D */
static void demo_btn_click(fly_widget_t *w) {
  (void)w;
  if (start_menu_win) {
    wm_destroy_window(start_menu_win);
    start_menu_win = NULL;
    start_menu_ctx = NULL;
  }
  demo3d_app_create();
}

/* Button Handler: Lansează Minesweeper */
static void mine_btn_click(fly_widget_t *w) {
  (void)w;
  if (start_menu_win) {
    wm_destroy_window(start_menu_win);
    start_menu_win = NULL;
    start_menu_ctx = NULL;
  }
  minesweeper_app_create();
}

/* Button Handler: Lansează Tic Tac Toe */
static void xo_btn_click(fly_widget_t *w) {
  (void)w;
  if (start_menu_win)
    wm_destroy_window(start_menu_win);
  start_menu_win = NULL;
  start_menu_ctx = NULL;
  tic_tac_toe_app_create();
}

static void create_desktop();
static void create_taskbar();

static void login_btn_click(fly_widget_t *w) {
  (void)w;
  const char *u = fly_textbox_get_text(login_user_box);
  const char *p = fly_textbox_get_text(login_pass_box);

  if (user_switch(u, p) == 0) {
    serial("[LOGIN] GUI Login Success for '%s'\n", u);
    wm_destroy_window(login_win);
    login_win = NULL;
    login_ctx = NULL;

    create_desktop();
    create_taskbar();
    wm_set_reserved_bottom(TASKBAR_H);
    wm_mark_dirty();
  } else {
    serial("[LOGIN] GUI Login Failed for '%s'\n", u);
    fly_label_set_text(login_msg_label, "Login Failed!");
    fly_textbox_set_text(login_pass_box, "");
    wm_mark_dirty();
  }
}

static void login_shutdown_click(fly_widget_t *w) {
  (void)w;
  cmd_shutdown(NULL);
}

static void create_login_screen() {
  gpu_device_t *gpu = gpu_get_primary();
  if (!gpu)
    return;

  int lw = 300;
  int lh = 200;
  int lx = (gpu->width - lw) / 2;
  int ly = (gpu->height - lh) / 2;

  surface_t *s = surface_create(lw, lh);
  if (!s)
    return;

  login_win = wm_create_window(s, lx, ly);
  wm_set_window_flags(login_win, WIN_FLAG_NO_DECOR | WIN_FLAG_NO_RESIZE);
  wm_set_title(login_win, "Login");
  login_win->w = lw;
  login_win->h = lh;

  login_ctx = flyui_init(login_win->surface);
  fly_widget_t *root = fly_panel_create(lw, lh);
  root->bg_color = 0xFFE0E0E0;
  flyui_set_root(login_ctx, root);

  /* Title */
  fly_widget_t *lbl_title = fly_label_create("Chrysalis OS Login");
  lbl_title->x = (lw - (18 * 8)) / 2;
  lbl_title->y = 20;
  fly_widget_add(root, lbl_title);

  /* Username */
  fly_widget_t *lbl_user = fly_label_create("Username:");
  lbl_user->x = 40;
  lbl_user->y = 60;
  fly_widget_add(root, lbl_user);

  login_user_box = fly_textbox_create(200, false);
  login_user_box->x = 40;
  login_user_box->y = 80;
  fly_widget_add(root, login_user_box);

  /* Password */
  fly_widget_t *lbl_pass = fly_label_create("Password:");
  lbl_pass->x = 40;
  lbl_pass->y = 110;
  fly_widget_add(root, lbl_pass);

  login_pass_box = fly_textbox_create(200, true);
  login_pass_box->x = 40;
  login_pass_box->y = 130;
  fly_widget_add(root, login_pass_box);

  /* Message Label */
  login_msg_label = fly_label_create("");
  login_msg_label->x = 40;
  login_msg_label->y = 160;
  login_msg_label->fg_color = 0xFFFF0000;
  fly_widget_add(root, login_msg_label);

  /* Login Button */
  fly_widget_t *btn_login = fly_button_create("Login");
  btn_login->x = 180;
  btn_login->y = 160;
  btn_login->w = 80;
  fly_button_set_callback(btn_login, login_btn_click);
  fly_widget_add(root, btn_login);

  /* Shutdown Button */
  fly_widget_t *btn_sh = fly_button_create("Power");
  btn_sh->x = 240;
  btn_sh->y = 10;
  btn_sh->w = 50;
  btn_sh->h = 20;
  fly_button_set_callback(btn_sh, login_shutdown_click);
  fly_widget_add(root, btn_sh);

  login_ctx->focused_widget = login_user_box;
  if (login_user_box)
    login_user_box->focused = true;

  flyui_render(login_ctx);
}

/* Taskbar Clock Widget Draw */
static void taskbar_clock_draw(fly_widget_t *w, surface_t *surf, int x, int y) {
  /* Background */
  fly_draw_rect_fill(surf, x, y, w->w, w->h, w->bg_color);

  datetime t;
  time_get_local(&t);

  char time_str[16];
  char date_str[16];
  char tmp[8];

  /* Time: HH:MM */
  time_str[0] = '0' + (t.hour / 10);
  time_str[1] = '0' + (t.hour % 10);
  time_str[2] = ':';
  time_str[3] = '0' + (t.minute / 10);
  time_str[4] = '0' + (t.minute % 10);
  time_str[5] = 0;

  /* Date: DD.MM.YYYY */
  date_str[0] = '0' + (t.day / 10);
  date_str[1] = '0' + (t.day % 10);
  date_str[2] = '.';
  date_str[3] = '0' + (t.month / 10);
  date_str[4] = '0' + (t.month % 10);
  date_str[5] = '.';

  itoa_dec(tmp, t.year);
  strcpy(date_str + 6, tmp);

  /* Draw Time (Top) */
  int time_w = strlen(time_str) * 8;
  int tx = x + (w->w - time_w) / 2;
  int ty = y + 4;
  fly_draw_text(surf, tx, ty, time_str, 0xFF000000);

  /* Draw Date (Bottom) */
  int date_w = strlen(date_str) * 8;
  int dx = x + (w->w - date_w) / 2;
  int dy = y + 20;
  fly_draw_text(surf, dx, dy, date_str, 0xFF000000);
}

/* Run Button Handler */
static void run_btn_click(fly_widget_t *w) {
  (void)w;
  run_dialog_app_create();
}

/* Network Popup Handlers */
static bool net_popup_close_event(fly_widget_t *w, fly_event_t *e) {
  (void)w;
  if (e->type == FLY_EVENT_MOUSE_UP) {
    if (net_win) {
      wm_destroy_window(net_win);
      net_win = NULL;
      net_ctx = NULL;
      wm_mark_dirty();
    }
    return true;
  }
  return false;
}

static void append_safe(char *dst, size_t cap, const char *src) {
  size_t len = strlen(dst);
  size_t i = 0;
  while (src[i] && (len + 1) < cap) {
    dst[len++] = src[i++];
  }
  dst[len] = 0;
}

static void ip_to_str(uint32_t ip, char *out, size_t cap) {
  if (!out || cap == 0)
    return;
  char buf[16];
  char tmp[16];
  out[0] = 0;

  itoa_dec(buf, (int32_t)(ip & 0xFF));
  strncpy(out, buf, cap);
  out[cap - 1] = 0;
  append_safe(out, cap, ".");

  itoa_dec(tmp, (int32_t)((ip >> 8) & 0xFF));
  append_safe(out, cap, tmp);
  append_safe(out, cap, ".");

  itoa_dec(tmp, (int32_t)((ip >> 16) & 0xFF));
  append_safe(out, cap, tmp);
  append_safe(out, cap, ".");

  itoa_dec(tmp, (int32_t)((ip >> 24) & 0xFF));
  append_safe(out, cap, tmp);
}

static void mac_to_str(const uint8_t mac[6], char *out, size_t cap) {
  static const char *hex = "0123456789abcdef";
  if (!out || cap < 18)
    return;
  int p = 0;
  for (int i = 0; i < 6; ++i) {
    uint8_t b = mac[i];
    if (p + 2 >= (int)cap)
      break;
    out[p++] = hex[(b >> 4) & 0xF];
    out[p++] = hex[b & 0xF];
    if (i < 5 && p + 1 < (int)cap)
      out[p++] = ':';
  }
  out[p] = 0;
}

static void create_net_popup() {
  if (net_win)
    return;

  fly_theme_t *th = theme_get();
  int w = 250;
  int h = 180;
  surface_t *s = surface_create(w, h);
  if (!s)
    return;
  surface_clear(s, th->win_bg);

  /* Border */
  fly_draw_rect_outline(s, 0, 0, w, h, th->color_hi_1);
  fly_draw_rect_outline(s, 0, 0, w - 1, h - 1, th->color_lo_2);

  net_ctx = flyui_init(s);
  fly_widget_t *root = fly_panel_create(w, h);
  root->bg_color = th->win_bg;
  flyui_set_root(net_ctx, root);

  /* Title */
  fly_widget_t *lbl_title = fly_label_create("Network Status");
  lbl_title->x = 10;
  lbl_title->y = 10;
  fly_widget_add(root, lbl_title);

  /* Close Button */
  fly_widget_t *btn_close = fly_button_create("X");
  btn_close->x = w - 30;
  btn_close->y = 5;
  btn_close->w = 25;
  btn_close->h = 25;
  btn_close->on_event = net_popup_close_event;
  fly_widget_add(root, btn_close);

  /* Data */
  net_device_t *dev = net_get_primary_device();
  char buf[64];
  int y = 40;

  if (dev) {
    fly_widget_t *lbl_stat = fly_label_create("Status: Connected");
    lbl_stat->x = 10;
    lbl_stat->y = y;
    fly_widget_add(root, lbl_stat);
    y += 20;

    uint32_t ip = dev->ip;
    ip_to_str(ip, buf, sizeof(buf));
    char line_ip[72];
    strncpy(line_ip, "IP: ", sizeof(line_ip));
    line_ip[sizeof(line_ip) - 1] = 0;
    append_safe(line_ip, sizeof(line_ip), buf);
    fly_widget_t *lbl_ip = fly_label_create(line_ip);
    lbl_ip->x = 10;
    lbl_ip->y = y;
    fly_widget_add(root, lbl_ip);
    y += 20;

    uint32_t gw = dev->gateway;
    ip_to_str(gw, buf, sizeof(buf));
    char line_gw[72];
    strncpy(line_gw, "GW: ", sizeof(line_gw));
    line_gw[sizeof(line_gw) - 1] = 0;
    append_safe(line_gw, sizeof(line_gw), buf);
    fly_widget_t *lbl_gw = fly_label_create(line_gw);
    lbl_gw->x = 10;
    lbl_gw->y = y;
    fly_widget_add(root, lbl_gw);
    y += 20;

    uint32_t dns = dev->dns_server;
    ip_to_str(dns, buf, sizeof(buf));
    char line_dns[72];
    strncpy(line_dns, "DNS: ", sizeof(line_dns));
    line_dns[sizeof(line_dns) - 1] = 0;
    append_safe(line_dns, sizeof(line_dns), buf);
    fly_widget_t *lbl_dns = fly_label_create(line_dns);
    lbl_dns->x = 10;
    lbl_dns->y = y;
    fly_widget_add(root, lbl_dns);
    y += 20;

    mac_to_str(dev->mac, buf, sizeof(buf));
    char line_mac[80];
    strncpy(line_mac, "MAC: ", sizeof(line_mac));
    line_mac[sizeof(line_mac) - 1] = 0;
    append_safe(line_mac, sizeof(line_mac), buf);
    fly_widget_t *lbl_mac = fly_label_create(line_mac);
    lbl_mac->x = 10;
    lbl_mac->y = y;
    fly_widget_add(root, lbl_mac);
  } else {
    fly_widget_t *lbl_stat = fly_label_create("Status: No Device");
    lbl_stat->x = 10;
    lbl_stat->y = y;
    fly_widget_add(root, lbl_stat);
  }

  flyui_render(net_ctx);

  gpu_device_t *gpu = gpu_get_primary();
  /* Position near bottom right, above taskbar */
  int sx = gpu->width - w - 10;
  int sy = gpu->height - TASKBAR_H - h - 5;
  net_win = wm_create_window(s, sx, sy);
  if (net_win) {
    wm_set_window_flags(net_win, WIN_FLAG_NO_DECOR | WIN_FLAG_NO_RESIZE);
    wm_set_title(net_win, "Network");
  }
}

static void net_btn_click(fly_widget_t *w) {
  (void)w;
  if (net_win) {
    wm_destroy_window(net_win);
    net_win = NULL;
    net_ctx = NULL;
    wm_mark_dirty();
  } else {
    create_net_popup();
  }
}

/* Start Menu Implementation */
static void start_menu_shutdown_click(fly_widget_t *w) {
  (void)w;
  serial("[WIN] Shutdown requested from Start Menu.\n");

  // Power off the system directly
  cmd_shutdown(nullptr);
}

static void create_start_menu() {
  if (start_menu_win) {
    wm_destroy_window(start_menu_win);
    start_menu_win = NULL;
    start_menu_ctx = NULL;
    start_menu_just_toggled = true;
    wm_mark_dirty();
    return;
  }

  fly_theme_t *th = theme_get();
  int w = 210;
  int h = 340;
  int header_h = 36;
  int side_w = 36;
  surface_t *s = surface_create(w, h);
  if (!s)
    return;

  start_menu_ctx = flyui_init(s);
  fly_widget_t *root = fly_widget_create();
  root->w = w;
  root->h = h;
  root->bg_color = th->win_bg;
  root->on_draw = start_menu_bg_draw;
  root->bg_color = th->win_bg;
  flyui_set_root(start_menu_ctx, root);

  /* Header */
  fly_widget_t *header = fly_widget_create();
  header->x = 0;
  header->y = 0;
  header->w = w;
  header->h = header_h;
  header->on_draw = start_menu_header_draw;
  fly_widget_add(root, header);

  fly_widget_t *title = fly_label_create("Chrysalis");
  title->x = 12;
  title->y = 10;
  title->fg_color = 0xFFFFFFFF;
  fly_widget_add(root, title);

  /* Side bar */
  fly_widget_t *side = fly_widget_create();
  side->x = 0;
  side->y = header_h;
  side->w = side_w;
  side->h = h - header_h;
  side->on_draw = start_menu_side_draw;
  fly_widget_add(root, side);

  int y = header_h + 8;
  int bh = 28;
  int bx = side_w + 8;
  int bw = w - bx - 8;

  /* Menu Items */
  fly_widget_t *btn;

  btn = fly_button_create("Terminal");
  btn->x = bx;
  btn->y = y;
  btn->w = bw;
  btn->h = bh;
  fly_button_set_callback(btn, terminal_btn_click);
  fly_widget_add(root, btn);
  y += bh + 6;
  btn = fly_button_create("Files");
  btn->x = bx;
  btn->y = y;
  btn->w = bw;
  btn->h = bh;
  fly_button_set_callback(btn, files_btn_click);
  fly_widget_add(root, btn);
  y += bh + 6;
  btn = fly_button_create("Notepad");
  btn->x = bx;
  btn->y = y;
  btn->w = bw;
  btn->h = bh;
  fly_button_set_callback(btn, note_btn_click);
  fly_widget_add(root, btn);
  y += bh + 6;
  btn = fly_button_create("Paint");
  btn->x = bx;
  btn->y = y;
  btn->w = bw;
  btn->h = bh;
  fly_button_set_callback(btn, paint_btn_click);
  fly_widget_add(root, btn);
  y += bh + 6;
  btn = fly_button_create("Calc");
  btn->x = bx;
  btn->y = y;
  btn->w = bw;
  btn->h = bh;
  fly_button_set_callback(btn, calc_btn_click);
  fly_widget_add(root, btn);
  y += bh + 6;
  btn = fly_button_create("Run...");
  btn->x = bx;
  btn->y = y;
  btn->w = bw;
  btn->h = bh;
  fly_button_set_callback(btn, run_btn_click);
  fly_widget_add(root, btn);
  y += bh + 6;
  btn = fly_button_create("X and 0");
  btn->x = bx;
  btn->y = y;
  btn->w = bw;
  btn->h = bh;
  fly_button_set_callback(btn, xo_btn_click);
  fly_widget_add(root, btn);
  y += bh + 6;

  y += 5;
  /* Separator */
  fly_widget_t *sep = fly_panel_create(bw, 2);
  sep->x = bx;
  sep->y = y;
  sep->bg_color = 0xFF808080;
  fly_widget_add(root, sep);
  y += 10;

  btn = fly_button_create("Shutdown");
  btn->x = bx;
  btn->y = y;
  btn->w = bw;
  btn->h = bh;
  fly_button_set_callback(btn, start_menu_shutdown_click);
  fly_widget_add(root, btn);

  flyui_render(start_menu_ctx);

  gpu_device_t *gpu = gpu_get_primary();
  /* Position above start button */
  start_menu_win = wm_create_window(s, 0, gpu->height - TASKBAR_H - h);
  if (start_menu_win) {
    wm_set_window_flags(start_menu_win, WIN_FLAG_NO_DECOR | WIN_FLAG_NO_RESIZE |
                                            WIN_FLAG_NO_DRAG);
    wm_set_title(start_menu_win, "Start");
  }
  start_menu_just_toggled = true;
}

static void start_btn_click(fly_widget_t *w) {
  (void)w;
  create_start_menu();
}

/* create_taskbar_btn removed */

static void desktop_draw(fly_widget_t *w, surface_t *surf, int x, int y) {
  (void)x;
  (void)y;
  /* Dark aesthetic blue gradient */
  fly_draw_rect_vgradient(surf, 0, 0, w->w, w->h, 0xFF435A6F, 0xFF202B36);

  /* Subtle watermark */
  fly_draw_text(surf, w->w - 180, w->h - 30, "Chrysalis OS v0.2 beta",
                0x20FFFFFF);
}

static void create_desktop() {
  gpu_device_t *gpu = gpu_get_primary();
  if (!gpu)
    return;

  int w = gpu->width;
  int h = gpu->height - TASKBAR_H;

  surface_t *s = surface_create(w, h);
  if (!s)
    return;

  desktop_ctx = flyui_init(s);
  fly_widget_t *root = fly_panel_create(w, h);
  root->on_draw = desktop_draw;
  flyui_set_root(desktop_ctx, root);

  /* TODO: Add Desktop Icons here using fly_icon_button_create */

  flyui_render(desktop_ctx);

  desktop_win = wm_create_window(s, 0, 0);
  if (desktop_win) {
    desktop_win->flags |=
        WIN_FLAG_NO_DECOR | WIN_FLAG_NO_RESIZE | WIN_FLAG_STAY_BOTTOM;
    wm_set_title(desktop_win, "Desktop");
  }
  /* Desktop is always at the bottom */
  wm_focus_window(desktop_win);
}
static void create_taskbar() {
  gpu_device_t *gpu = gpu_get_primary();
  if (!gpu)
    return;

  fly_theme_t *th = theme_get();
  int w = gpu->width;
  int h = TASKBAR_H;

  /* 1. Create Surface */
  surface_t *s = surface_create(w, h);
  if (!s)
    return;

  /* Aero-like taskbar background */
  uint32_t tb_top = shade_color(0xFF2A3A4B, 10);
  uint32_t tb_bot = shade_color(0xFF1D2833, -5);
  fly_draw_rect_vgradient(s, 0, 0, w, h, tb_top, tb_bot);
  /* Top highlight line */
  fly_draw_line(s, 0, 0, w, 0, th->color_hi_1);
  /* Bottom shadow line */
  fly_draw_line(s, 0, h - 1, w, h - 1, th->color_lo_2);

  /* 2. Init FlyUI */
  taskbar_ctx = flyui_init(s);

  /* 3. Create Widgets */
  fly_widget_t *root = fly_panel_create(w, h);
  root->bg_color = tb_bot;
  flyui_set_root(taskbar_ctx, root);

  int x = 6;
  int y = 2;
  int bw = 28; /* Icon button width */
  int bh = 28; /* Icon button height */

  /* Start Button */
  fly_widget_t *btn_start = fly_icon_button_create(ICON_START, "Start");
  btn_start->x = x;
  btn_start->y = y;
  btn_start->w = 48;
  btn_start->h = bh;
  fly_button_set_callback(btn_start, start_btn_click);
  fly_widget_add(root, btn_start);
  x += 48 + 6;

  auto add_tb_icon = [&](int icon, void (*cb)(fly_widget_t *)) {
    fly_widget_t *b = fly_icon_button_create(icon, NULL);
    b->x = x;
    b->y = y;
    b->w = bw;
    b->h = bh;
    fly_button_set_callback(b, cb);
    fly_widget_add(root, b);
    x += bw;
  };

  add_tb_icon(ICON_RUN, run_btn_click);
  add_tb_icon(ICON_TERM, terminal_btn_click);
  add_tb_icon(ICON_FILES, files_btn_click);
  add_tb_icon(ICON_IMG, img_btn_click);
  add_tb_icon(ICON_NOTE, note_btn_click);
  add_tb_icon(ICON_PAINT, paint_btn_click);
  add_tb_icon(ICON_CALC, calc_btn_click);
  add_tb_icon(ICON_CLOCK, clock_btn_click);
  add_tb_icon(ICON_CAL, cal_btn_click);
  add_tb_icon(ICON_TASK, task_btn_click);
  add_tb_icon(ICON_INFO, info_btn_click);
  add_tb_icon(ICON_3D, demo_btn_click);
  add_tb_icon(ICON_MINE, mine_btn_click);
  add_tb_icon(ICON_XO, xo_btn_click);

  /* Net (Right Aligned) */
  fly_widget_t *btn_net = fly_icon_button_create(ICON_NET, NULL);
  btn_net->x = w - 170;
  btn_net->y = y;
  btn_net->w = bw;
  btn_net->h = bh;
  fly_button_set_callback(btn_net, net_btn_click);
  fly_widget_add(root, btn_net);
  /* Clock Widget (Right Aligned) */
  fly_widget_t *sys_clock = fly_panel_create(110, h);
  sys_clock->x = w - 115; /* 5px margin from right */
  sys_clock->y = 0;
  sys_clock->bg_color = 0xFF1D2833;
  sys_clock->on_draw = taskbar_clock_draw;
  fly_widget_add(root, sys_clock);

  /* 4. Initial Render */
  flyui_render(taskbar_ctx);

  /* 5. Create WM Window */
  /* Position at bottom of screen */
  taskbar_win = wm_create_window(s, 0, gpu->height - h);
  if (taskbar_win) {
    wm_set_window_flags(taskbar_win, WIN_FLAG_NO_DECOR | WIN_FLAG_NO_RESIZE);
    wm_set_title(taskbar_win, "Taskbar");
  }
}

extern "C" int cmd_launch_exit(int argc, char **argv) {
  (void)argc;
  (void)argv;
  if (is_gui_running) {
    is_gui_running = false;
    return 0;
  }
  terminal_writestring("GUI is not running.\n");
  return 1;
}

extern "C" int cmd_logoff(int argc, char **argv) {
  (void)argc;
  (void)argv;

  serial("[WIN] Logoff requested (GUI: %d)\n", is_gui_running);

  if (is_gui_running) {
    /* GUI Logoff Flow */
    if (start_menu_win) {
      wm_destroy_window(start_menu_win);
      start_menu_win = NULL;
    }
    if (net_win) {
      wm_destroy_window(net_win);
      net_win = NULL;
    }
    if (taskbar_win) {
      wm_destroy_window(taskbar_win);
      taskbar_win = NULL;
    }
    if (desktop_win) {
      wm_destroy_window(desktop_win);
      desktop_win = NULL;
    }
    /* Note: Ideally we would notify apps to close, but for now we just clean up
     * sys UI */

    user_logout();
    create_login_screen();
  } else {
    /* Text mode logoff is handled by shell logic if it detects user change,
       but here we just ensure user is reset. */
    user_logout();
    terminal_printf("Logged out successfully.\n");
  }

  return 0;
}

extern "C" int cmd_launch(int argc, char **argv) {
  (void)argc;
  (void)argv;

  if (apic_is_forced_off()) {
    terminal_writestring(
        "GUI disabled: APIC forced off (apic=off). Use text mode.\n");
    return 1;
  }

  if (is_gui_running) {
    terminal_writestring("GUI is already running.\n");
    return 1;
  }
  is_gui_running = true;

  serial("[WIN] Starting GUI environment...\n");

  /* 1. Disable Terminal Rendering (Text Mode OFF) - until shell window is
   * opened */
  terminal_set_rendering(false);

  /* 2. Initialize GUI Subsystems */
  theme_init();
  compositor_init();
  wm_init();

  /* Load Icons */
  if (icons_init("/icons.mod")) {
    serial("[WIN] Icons loaded successfully.\n");
  } else {
    serial("[WIN] Warning: icons not found or invalid.\n");
  }

  app_manager_init();

  /* 3. Initial View: Login if not authenticated */
  if (user_get_current() == NULL) {
    create_login_screen();
  } else {
    create_desktop();
    create_taskbar();
    wm_set_reserved_bottom(TASKBAR_H);
  }

  /* 4. Main GUI Loop */
  input_event_t ev;

  /* Force initial render */
  wm_mark_dirty();
  wm_render();

  /* State for Window Dragging */
  window_t *drag_win = NULL;
  int drag_off_x = 0;
  int drag_off_y = 0;
  window_t *resize_win = NULL;
  int resize_start_w = 0;
  int resize_start_h = 0;
  int resize_start_mx = 0;
  int resize_start_my = 0;

  uint64_t last_icon_ms = 0;
  while (is_gui_running) {
    /* Update Subsystems (Missing Pollers) */
    usb_poll();
    io_sched_poll();
    net_poll();
    ps2_controller_watchdog();

    /* Update Apps */
    clock_app_update();
    demo3d_app_update();
    task_manager_app_update();

    /* Lazy icon loading to keep UI responsive */
    uint64_t now_ms = timer_uptime_ms();
    if (now_ms != 0 && (now_ms - last_icon_ms) >= 50) {
      last_icon_ms = now_ms;
      if (icons_tick(1)) {
        if (taskbar_ctx) {
          flyui_render(taskbar_ctx);
        }
        wm_mark_dirty();
      }
    }

    /* Update Taskbar Clock */
    datetime t;
    time_get_local(&t);
    if (t.minute != taskbar_last_min) {
      taskbar_last_min = t.minute;
      /* Redraw taskbar to update clock */
      flyui_render(taskbar_ctx);
      wm_mark_dirty();
    }

    /* Poll Input */
    while (input_pop(&ev)) {
      /* Handle Global Keys */
      if (ev.type == INPUT_KEYBOARD && ev.pressed) {
        if (ev.keycode == 0x58) { /* F12 to Exit */
          is_gui_running = false;
        }

        /* Route keyboard to Focused Window */
        window_t *focused = wm_get_focused();
        if (focused == shell_get_window()) {
          shell_handle_char((char)ev.keycode);
        } else if (focused == notepad_app_get_window()) {
          notepad_app_handle_key((char)ev.keycode);
        } else if (focused == run_dialog_app_get_window()) {
          run_dialog_app_handle_key((char)ev.keycode);
        } else if (focused == login_win && login_ctx) {
          fly_event_t fev;
          fev.type = FLY_EVENT_KEY_DOWN;
          fev.keycode = ev.keycode;
          flyui_dispatch_event(login_ctx, &fev);
          flyui_render(login_ctx);
          wm_mark_dirty();
        } else if (focused) {
          /* Route to standalone app via window queue */
          window_push_event(focused, &ev);
        }
      }

      /* Mouse Event Handling */
      if (ev.type == INPUT_MOUSE_MOVE || ev.type == INPUT_MOUSE_CLICK) {

        /* Handle Dragging Logic */
        if (drag_win && ev.type == INPUT_MOUSE_MOVE) {
          drag_win->x = ev.mouse_x - drag_off_x;
          drag_win->y = ev.mouse_y - drag_off_y;
          wm_mark_dirty();
        }

        /* Always mark dirty on mouse move to update cursor position */
        if (ev.type == INPUT_MOUSE_MOVE) {
          wm_mark_dirty();
        }

        /* Handle Resizing */
        if (resize_win && ev.type == INPUT_MOUSE_MOVE) {
          int dx = ev.mouse_x - resize_start_mx;
          int dy = ev.mouse_y - resize_start_my;
          int new_w = resize_start_w + dx;
          int new_h = resize_start_h + dy;
          wm_resize_window(resize_win, new_w, new_h);
        }

        /* 1. Find Window Under Mouse (Top-most) if not dragging */
        window_t *target = (drag_win || resize_win)
                               ? (drag_win ? drag_win : resize_win)
                               : wm_find_window_at(ev.mouse_x, ev.mouse_y);

        /* 2. Handle Focus & Drag Start on Click */
        if (ev.type == INPUT_MOUSE_CLICK) {
          if (ev.pressed) {
            /* Handle Scroll (Buttons 4 & 5) */
            if (ev.keycode == 4 || ev.keycode == 5) {
              if (target == shell_get_window()) {
                /* Map Scroll to History Navigation (Ctrl-P / Ctrl-N) */
                char key =
                    (ev.keycode == 4) ? 16 /* Ctrl-P */ : 14 /* Ctrl-N */;
                shell_handle_char(key);
              }
            }
            /* Handle Left Click (Button 1) for Focus/Drag */
            else if (ev.keycode == 1) {
              if (target) {
                wm_focus_window(target);
                wm_mark_dirty();

                int rel_x = ev.mouse_x - target->x;
                int rel_y = ev.mouse_y - target->y;

                if (target != taskbar_win && wm_window_is_decorated(target)) {
                  int title_h = theme_get()->title_height;
                  if (rel_y >= 0 && rel_y < title_h) {
                    if (wm_chrome_handle_event(target, rel_x, rel_y,
                                               ev.pressed)) {
                      continue;
                    }
                  }

                  /* Resize grip (bottom-right) */
                  if ((target->flags & WIN_FLAG_NO_RESIZE) == 0) {
                    if (rel_x >= target->w - WM_RESIZE_GRIP &&
                        rel_y >= target->h - WM_RESIZE_GRIP) {
                      resize_win = target;
                      resize_start_w = target->w;
                      resize_start_h = target->h;
                      resize_start_mx = ev.mouse_x;
                      resize_start_my = ev.mouse_y;
                      continue;
                    }
                  }
                }

                /* Check for Title Bar Hit (0-24px relative to window) */
                if (rel_y >= 0 && rel_y < theme_get()->title_height &&
                    target != taskbar_win && /* Fix: Don't drag taskbar */
                    !(target->flags &
                      WIN_FLAG_NO_DRAG)) { /* Fix: Don't drag popups */
                  drag_win = target;
                  drag_off_x = ev.mouse_x - target->x;
                  drag_off_y = ev.mouse_y - target->y;
                }
              }
            }
          } else {
            window_t *chrome_target = target ? target : wm_get_focused();
            if (chrome_target && chrome_target != taskbar_win &&
                wm_window_is_decorated(chrome_target)) {
              int rel_x = ev.mouse_x - chrome_target->x;
              int rel_y = ev.mouse_y - chrome_target->y;
              wm_chrome_handle_event(chrome_target, rel_x, rel_y, false);
            }
            /* Mouse Up: Stop Dragging */
            drag_win = NULL;
            resize_win = NULL;
          }
        }

        /* 3. Dispatch Mouse Events
         * While a window is actively being resized by the WM grip, do not
         * forward raw mouse events to the app. This prevents event-queue flood
         * (mouse move spam) that can starve/drop WINDOW_RESIZE events. */
        bool suppress_app_mouse_dispatch = (resize_win != NULL);

        if (!suppress_app_mouse_dispatch) {
          /* 3.1 Apps */
          if (target == clock_app_get_window())
            clock_app_handle_event(&ev);
          if (target == shell_get_window()) {
            if (shell_handle_event(&ev))
              target = NULL;
          }
          if (target == calculator_app_get_window())
            calculator_app_handle_event(&ev);
          if (target == notepad_app_get_window())
            notepad_app_handle_event(&ev);
          if (target == calendar_app_get_window())
            calendar_app_handle_event(&ev);
          if (target == file_manager_app_get_window())
            file_manager_app_handle_event(&ev);
          if (target == image_viewer_app_get_window())
            image_viewer_app_handle_event(&ev);
          if (target == sysinfo_app_get_window())
            sysinfo_app_handle_event(&ev);
          if (target == run_dialog_app_get_window())
            run_dialog_app_handle_event(&ev);
          if (target == task_manager_app_get_window())
            task_manager_app_handle_event(&ev);
          if (target == paint_app_get_window())
            paint_app_handle_event(&ev);
          if (target == demo3d_app_get_window())
            demo3d_app_handle_event(&ev);
          if (target == minesweeper_app_get_window())
            minesweeper_app_handle_event(&ev);
          if (target == tic_tac_toe_app_get_window())
            tic_tac_toe_app_handle_event(&ev);

          /* 3.2 FlyUI based windows (Login, Net, Start, Popups) */
          struct {
            void operator()(window_t *win, flyui_context_t *ctx,
                            input_event_t &ev) {
              if (win && ctx) {
                fly_event_t fev;
                fev.mx = ev.mouse_x - win->x;
                fev.my = ev.mouse_y - win->y;
                fev.keycode = 0;
                fev.type = FLY_EVENT_NONE;
                if (ev.type == INPUT_MOUSE_MOVE)
                  fev.type = FLY_EVENT_MOUSE_MOVE;
                else if (ev.type == INPUT_MOUSE_CLICK)
                  fev.type =
                      ev.pressed ? FLY_EVENT_MOUSE_DOWN : FLY_EVENT_MOUSE_UP;

                if (fev.type != FLY_EVENT_NONE) {
                  flyui_dispatch_event(ctx, &fev);
                  if (fev.type != FLY_EVENT_MOUSE_MOVE) {
                    flyui_render(ctx);
                    wm_mark_dirty();
                  }
                }
              }
            }
          } dispatch_flyui_fn;

          if (target == login_win)
            dispatch_flyui_fn(login_win, login_ctx, ev);
          if (target == popup_win)
            dispatch_flyui_fn(popup_win, popup_ctx, ev);
          if (target == net_win)
            dispatch_flyui_fn(net_win, net_ctx, ev);
          if (target == start_menu_win)
            dispatch_flyui_fn(start_menu_win, start_menu_ctx, ev);

          if (target == taskbar_win && taskbar_ctx && !drag_win) {
            dispatch_flyui_fn(taskbar_win, taskbar_ctx, ev);
          }
          if (target == desktop_win && desktop_ctx && !drag_win) {
            dispatch_flyui_fn(desktop_win, desktop_ctx, ev);
          }

          /* 3.3 Generic Dispatch for Standalone Apps */
          /* If window has an owner task and wasn't handled by hardcoded internal
           * handlers, dispatch to its task event queue. */
          if (target && target->owner && target != clock_app_get_window() &&
              target != shell_get_window() &&
              target != calculator_app_get_window() &&
              target != notepad_app_get_window() &&
              target != calendar_app_get_window() &&
              target != file_manager_app_get_window() &&
              target != image_viewer_app_get_window() &&
              target != sysinfo_app_get_window() &&
              target != run_dialog_app_get_window() &&
              target != task_manager_app_get_window() &&
              target != paint_app_get_window() &&
              target != demo3d_app_get_window() &&
              target != minesweeper_app_get_window() &&
              target != tic_tac_toe_app_get_window() && target != login_win &&
              target != popup_win && target != net_win &&
              target != start_menu_win && target != taskbar_win &&
              target != desktop_win) {
            window_push_event(target, &ev);
          }
        }

        /* Close start menu when clicking outside it */
        if (ev.type == INPUT_MOUSE_CLICK && ev.pressed) {
          if (start_menu_win && !start_menu_just_toggled &&
              target != start_menu_win) {
            wm_destroy_window(start_menu_win);
            start_menu_win = NULL;
            start_menu_ctx = NULL;
            wm_mark_dirty();
          }
          start_menu_just_toggled = false;
        }
      }
    }

    /* Render GUI */
    if (wm_is_dirty()) {
      wm_render();
    }

    yield();
  }

  /* 5. Cleanup & Return to Text Mode */
  if (taskbar_win)
    wm_destroy_window(taskbar_win);
  taskbar_win = NULL;
  taskbar_ctx = NULL;
  if (popup_win)
    wm_destroy_window(popup_win);
  popup_win = NULL;
  popup_ctx = NULL;
  if (net_win)
    wm_destroy_window(net_win);
  net_win = NULL;
  net_ctx = NULL;
  if (start_menu_win)
    wm_destroy_window(start_menu_win);
  start_menu_win = NULL;
  start_menu_ctx = NULL;

  if (desktop_win)
    wm_destroy_window(desktop_win);
  desktop_win = NULL;
  desktop_ctx = NULL;
  if (login_win)
    wm_destroy_window(login_win);
  login_win = NULL;
  login_ctx = NULL;

  /* Dacă terminalul a fost deschis, îl închidem curat */
  if (shell_is_window_active()) {
    shell_destroy_window();
  }

  terminal_set_rendering(true);
  terminal_clear(); /* Clear artifacts */
  serial("[WIN] GUI shutdown. Returning to text mode.\n");
  is_gui_running = false;
  return 0;
}

bool win_is_gui_running(void) { return is_gui_running; }
