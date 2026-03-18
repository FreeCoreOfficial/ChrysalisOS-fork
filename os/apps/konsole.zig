const MAX_COLS: usize = 120;
const MAX_ROWS: usize = 64;
const MIN_COLS: i32 = 20;
const MIN_ROWS: i32 = 6;
const CHAR_W: i32 = 8;
const CHAR_H: i32 = 16;
const PAD_X: i32 = 10;
const PAD_Y: i32 = 10;

const P_INPUT_KEYBOARD: i32 = 0;
const P_INPUT_WINDOW_RESIZE: i32 = 4;

const PInputEvent = extern struct {
    type: i32,
    keycode: u32,
    pressed: u8,
    _pad: [3]u8,
    mouse_x: i32,
    mouse_y: i32,
};

extern fn p_exit(code: i32) callconv(.c) void;
extern fn p_write(s: [*:0]const u8) callconv(.c) void;
extern fn p_wm_create_window(w: i32, h: i32, x: i32, y: i32, title: [*:0]const u8) callconv(.c) ?*anyopaque;
extern fn p_wm_destroy_window(win: ?*anyopaque) callconv(.c) void;
extern fn p_wm_mark_dirty() callconv(.c) void;
extern fn p_wm_get_size(win: ?*anyopaque, w: *i32, h: *i32) callconv(.c) void;
extern fn p_draw_rect_fill(win: ?*anyopaque, x: i32, y: i32, w: i32, h: i32, color: u32) callconv(.c) void;
extern fn p_draw_text(win: ?*anyopaque, x: i32, y: i32, text: [*:0]const u8, color: u32) callconv(.c) void;
extern fn p_get_event(ev: *PInputEvent) callconv(.c) i32;
extern fn p_sleep(ms: u32) callconv(.c) void;
extern fn p_exec_command_capture(line: [*:0]const u8, out: [*]u8, out_cap: u32) callconv(.c) i32;
extern fn p_user_is_logged() callconv(.c) i32;

var screen: [MAX_ROWS][MAX_COLS + 1]u8 = undefined;
var cols: i32 = 60;
var rows: i32 = 20;
var win_w: i32 = 60 * CHAR_W + PAD_X * 2;
var win_h: i32 = 20 * CHAR_H + PAD_Y * 2;
var cursor_x: i32 = 0;
var cursor_y: i32 = 0;
var cmdline: [256]u8 = undefined;
var cmd_len: i32 = 0;
var cmd_output: [4096]u8 = undefined;

fn clampi(v: i32, lo: i32, hi: i32) i32 {
    if (v < lo) return lo;
    if (v > hi) return hi;
    return v;
}

fn clear_row(y: i32) void {
    if (y < 0 or y >= @as(i32, @intCast(MAX_ROWS))) return;
    const uy: usize = @as(usize, @intCast(y));
    var x: usize = 0;
    while (x < MAX_COLS) : (x += 1) {
        screen[uy][x] = ' ';
    }
    screen[uy][MAX_COLS] = 0;
}

fn clear_screen() void {
    var y: usize = 0;
    while (y < MAX_ROWS) : (y += 1) {
        clear_row(@as(i32, @intCast(y)));
    }
}

fn scroll_visible() void {
    if (rows <= 1) return;
    var y: i32 = 0;
    while (y < rows - 1) : (y += 1) {
        var x: usize = 0;
        while (x < MAX_COLS) : (x += 1) {
            const src_y: usize = @as(usize, @intCast(y + 1));
            const dst_y: usize = @as(usize, @intCast(y));
            screen[dst_y][x] = screen[src_y][x];
        }
    }
    clear_row(rows - 1);
    cursor_y = rows - 1;
    if (cursor_y < 0) cursor_y = 0;
}

fn apply_resize(new_w: i32, new_h: i32) void {
    if (new_w > 0) win_w = new_w;
    if (new_h > 0) win_h = new_h;

    var new_cols: i32 = @divTrunc(win_w - PAD_X * 2, CHAR_W);
    var new_rows: i32 = @divTrunc(win_h - PAD_Y * 2, CHAR_H);
    new_cols = clampi(new_cols, MIN_COLS, @as(i32, @intCast(MAX_COLS)));
    new_rows = clampi(new_rows, MIN_ROWS, @as(i32, @intCast(MAX_ROWS)));

    cols = new_cols;
    rows = new_rows;

    if (cursor_x >= cols) cursor_x = cols - 1;
    if (cursor_x < 0) cursor_x = 0;
    while (cursor_y >= rows) {
        scroll_visible();
    }
}

fn draw_screen(win: ?*anyopaque) void {
    var line: [MAX_COLS + 1]u8 = undefined;
    p_draw_rect_fill(win, 0, 0, win_w, win_h, 0xFF000000);
    var y: i32 = 0;
    while (y < rows) : (y += 1) {
        var x: i32 = 0;
        while (x < cols) : (x += 1) {
            const uy: usize = @as(usize, @intCast(y));
            const ux: usize = @as(usize, @intCast(x));
            line[ux] = screen[uy][ux];
        }
        const clen: usize = @as(usize, @intCast(cols));
        line[clen] = 0;
        const ptr: [*:0]const u8 = @ptrCast(&line[0]);
        p_draw_text(win, PAD_X, PAD_Y + y * CHAR_H, ptr, 0xFF00FF00);
    }
}

fn ks_putchar(c: u8) void {
    if (c == '\n') {
        cursor_x = 0;
        cursor_y += 1;
    } else if (c == '\r') {
        cursor_x = 0;
    } else {
        if (cursor_x >= cols) {
            cursor_x = 0;
            cursor_y += 1;
        }
        if (cursor_y >= rows) {
            scroll_visible();
        }
        if (cursor_y >= 0 and cursor_y < rows and cursor_x >= 0 and cursor_x < cols) {
            const uy: usize = @as(usize, @intCast(cursor_y));
            const ux: usize = @as(usize, @intCast(cursor_x));
            screen[uy][ux] = c;
            cursor_x += 1;
        }
    }

    if (cursor_x >= cols) {
        cursor_x = 0;
        cursor_y += 1;
    }
    if (cursor_y >= rows) {
        scroll_visible();
    }
}

fn print_str(s: [*:0]const u8) void {
    var i: usize = 0;
    while (s[i] != 0) : (i += 1) {
        ks_putchar(s[i]);
    }
}

fn print_prompt() void {
    print_str("> ");
}

fn exec_current_command() void {
    cmdline[@as(usize, @intCast(cmd_len))] = 0;
    ks_putchar('\n');

    if (cmd_len > 0) {
        const line_ptr: [*:0]const u8 = @ptrCast(&cmdline[0]);
        const out_ptr: [*]u8 = @ptrCast(&cmd_output[0]);
        var n: i32 = p_exec_command_capture(line_ptr, out_ptr, @as(u32, @intCast(cmd_output.len)));
        if (n > 0) {
            if (n >= @as(i32, @intCast(cmd_output.len))) n = @as(i32, @intCast(cmd_output.len)) - 1;
            cmd_output[@as(usize, @intCast(n))] = 0;
            print_str(@ptrCast(&cmd_output[0]));
            if (n > 0 and cmd_output[@as(usize, @intCast(n - 1))] != '\n') {
                ks_putchar('\n');
            }
        } else if (n < 0) {
            print_str("Command execution error.\n");
        }
    }

    cmd_len = 0;
    cmdline[0] = 0;
    print_prompt();
}

pub export fn main() callconv(.c) i32 {
    @setRuntimeSafety(false);
    const win = p_wm_create_window(win_w, win_h, 50, 50, "Konsole");
    if (win == null) {
        return 1;
    }

    var w: i32 = 0;
    var h: i32 = 0;
    p_wm_get_size(win, &w, &h);
    apply_resize(w, h);

    clear_screen();
    cmdline[0] = 0;
    cmd_len = 0;
    print_str("ChrysalisOS Konsole v0.2\n");

    if (p_user_is_logged() == 0) {
        print_str("No user logged in. Use GUI login.\n");
    }

    print_prompt();
    draw_screen(win);
    p_wm_mark_dirty();

    var ev: PInputEvent = undefined;
    var running: bool = true;
    while (running) {
        var polled_w: i32 = 0;
        var polled_h: i32 = 0;
        p_wm_get_size(win, &polled_w, &polled_h);
        if (polled_w < 120) polled_w = 120;
        if (polled_h < 80) polled_h = 80;
        if (polled_w != win_w or polled_h != win_h) {
            apply_resize(polled_w, polled_h);
            draw_screen(win);
            p_wm_mark_dirty();
        }

        if (p_get_event(&ev) != 0) {
            if (ev.type == P_INPUT_WINDOW_RESIZE) {
                apply_resize(ev.mouse_x, ev.mouse_y);
                draw_screen(win);
                p_wm_mark_dirty();
            } else if (ev.type == P_INPUT_KEYBOARD and ev.pressed != 0) {
                if (ev.keycode == 0x01 or ev.keycode == 0x1B) {
                    running = false;
                } else if (ev.keycode == 0x08) {
                    if (cmd_len > 0) {
                        cmd_len -= 1;
                        cmdline[@as(usize, @intCast(cmd_len))] = 0;
                        if (cursor_x > 0) {
                            cursor_x -= 1;
                            screen[@as(usize, @intCast(cursor_y))][@as(usize, @intCast(cursor_x))] = ' ';
                        } else if (cursor_y > 0) {
                            cursor_y -= 1;
                            cursor_x = cols - 1;
                            screen[@as(usize, @intCast(cursor_y))][@as(usize, @intCast(cursor_x))] = ' ';
                        }
                    }
                } else if (ev.keycode == '\n' or ev.keycode == '\r') {
                    exec_current_command();
                } else if (ev.keycode >= 32 and ev.keycode < 127) {
                    if (cmd_len < @as(i32, @intCast(cmdline.len)) - 1) {
                        const c: u8 = @as(u8, @intCast(ev.keycode));
                        cmdline[@as(usize, @intCast(cmd_len))] = c;
                        cmd_len += 1;
                        ks_putchar(c);
                    }
                }
                draw_screen(win);
                p_wm_mark_dirty();
            }
            // Avoid busy-loop on mouse move storms
            p_sleep(1);
        } else {
            p_sleep(50);
        }
    }

    p_wm_destroy_window(win);
    p_exit(0);
    return 0;
}
