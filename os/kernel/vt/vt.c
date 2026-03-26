/* kernel/vt/vt.c
 * Virtual Terminals Subsystem
 * Manages multiple text consoles over a single framebuffer.
 */

#include "vt.h"
#include "../video/fb_console.h"
#include "../drivers/serial.h"
#ifndef __x86_64__
#include "../shell/shell.h"
#else
static void shell_init_context(int id) { (void)id; }
static void shell_set_active_context(int id) { (void)id; }
static void shell_handle_char(char c) { (void)c; }
#endif
#include "../string.h"
#include "../colors/cl.h"

/* Max dimensions for static allocation */
#define VT_MAX_COLS 160
#define VT_MAX_ROWS 60

typedef struct {
    console_cell_t buffer[VT_MAX_ROWS * VT_MAX_COLS];
    uint32_t cursor_x;
    uint32_t cursor_y;
    int id;
    /* Shell context ID (0..VT_COUNT-1) */
    int shell_id;
    char role[32];
} vt_t;

static vt_t vts[VT_COUNT];
static int active_vt_id = 0;
static uint32_t screen_cols = 80;
static uint32_t screen_rows = 25;

/* Import serial logging */
extern void serial(const char *fmt, ...);

void vt_init(void) {
    serial("[VT] Initializing %d VTs...\n", VT_COUNT);

    /* Get actual screen dimensions from fb_console */
    fb_cons_get_dims(&screen_cols, &screen_rows);
    if (screen_cols > VT_MAX_COLS) screen_cols = VT_MAX_COLS;
    if (screen_rows > VT_MAX_ROWS) screen_rows = VT_MAX_ROWS;

    for (int i = 0; i < VT_COUNT; i++) {
        vts[i].id = i;
        vts[i].cursor_x = 0;
        vts[i].cursor_y = 0;
        vts[i].shell_id = i;
        
        /* Set default roles */
        if (i == 0) strcpy(vts[i].role, "shell");
        else if (i == 1) strcpy(vts[i].role, "klog");
        else if (i == 2) strcpy(vts[i].role, "debug");
        else if (i == 3) strcpy(vts[i].role, "script");
        else strcpy(vts[i].role, "free");

        /* Initialize buffer with spaces */
        for (uint32_t j = 0; j < VT_MAX_ROWS * VT_MAX_COLS; j++) {
            vts[i].buffer[j].c = ' ';
            vts[i].buffer[j].attr = cl_default();
        }
        
        /* Initialize shell context for this VT */
        shell_init_context(i);
    }

    /* Set VT0 as active and link fb_console to it */
    active_vt_id = 0;
    fb_cons_set_state(vts[0].buffer, &vts[0].cursor_x, &vts[0].cursor_y);
    shell_set_active_context(0);
    
    serial("[VT] Init done. Active: VT%d\n", active_vt_id + 1);
}

void vt_switch(int id) {
    if (id < 0 || id >= VT_COUNT) return;
    if (id == active_vt_id) return;

    serial("[VT] Switching from VT%d to VT%d\n", active_vt_id + 1, id + 1);

    active_vt_id = id;

    /* 1. Point fb_console to the new VT's state */
    fb_cons_set_state(vts[id].buffer, &vts[id].cursor_x, &vts[id].cursor_y);

    /* 2. Switch shell context */
    shell_set_active_context(vts[id].shell_id);

    /* 3. Force redraw */
    fb_cons_redraw();
}

int vt_active(void) {
    return active_vt_id;
}

void vt_putc(char c) {
    /* Always write to the active VT via fb_console.
       Since fb_console now points to the active VT's buffer,
       this updates the VT state AND draws to screen. */
    fb_cons_putc(c);
}

void vt_write(const char* s) {
    while (*s) vt_putc(*s++);
}

/* Route input to the active VT's shell */
void vt_shell_handle_char(char c) {
    /* Input goes to the shell of the active VT.
       Since we switch shell context in vt_switch,
       we just call the standard shell handler which uses the active context. */
    shell_handle_char(c);
}

/* Optional: Helper to log to a specific VT (background) */
void vt_log_to(int id, const char* s) {
    /* Not implemented in minimal scope: requires duplicating fb_cons logic
       or temporarily switching state without drawing.
       For now, logs go to active VT via terminal_writestring. */
    (void)id; (void)s;
}

void vt_clear(int id) {
    if (id < 0 || id >= VT_COUNT) return;
    
    if (id == active_vt_id) {
        fb_cons_clear();
    } else {
        /* Clear background buffer */
        for (uint32_t j = 0; j < VT_MAX_ROWS * VT_MAX_COLS; j++) {
            vts[id].buffer[j].c = ' ';
            vts[id].buffer[j].attr = cl_default();
        }
        vts[id].cursor_x = 0;
        vts[id].cursor_y = 0;
    }
}

const char* vt_get_role(int id) {
    if (id < 0 || id >= VT_COUNT) return "invalid";
    return vts[id].role;
}

void vt_set_role(int id, const char* role) {
    if (id < 0 || id >= VT_COUNT) return;
    strncpy(vts[id].role, role, 31);
    vts[id].role[31] = 0;
}