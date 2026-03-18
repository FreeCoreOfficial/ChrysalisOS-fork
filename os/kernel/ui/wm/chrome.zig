const surface_t = extern struct {
    width: u32,
    height: u32,
    x: i32,
    y: i32,
    visible: bool,
    pixels: [*]u32,
};

fn clampColor(v: i32) u32 {
    if (v < 0) return 0;
    if (v > 255) return 255;
    return @as(u32, @intCast(v));
}

fn shadeColor(color: u32, delta: i32) u32 {
    const a: u32 = (color >> 24) & 0xFF;
    var r: i32 = @as(i32, @intCast((color >> 16) & 0xFF));
    var g: i32 = @as(i32, @intCast((color >> 8) & 0xFF));
    var b: i32 = @as(i32, @intCast(color & 0xFF));
    r += delta;
    g += delta;
    b += delta;
    return (a << 24) | (clampColor(r) << 16) | (clampColor(g) << 8) | clampColor(b);
}

fn rectFill(surf: *surface_t, x: i32, y: i32, w: i32, h: i32, color: u32) void {
    if (w <= 0 or h <= 0) return;
    var sx = x;
    var sy = y;
    var sw = w;
    var sh = h;
    const surf_w: i32 = @as(i32, @intCast(surf.width));
    const surf_h: i32 = @as(i32, @intCast(surf.height));
    if (sx < 0) {
        sw += sx;
        sx = 0;
    }
    if (sy < 0) {
        sh += sy;
        sy = 0;
    }
    if (sx + sw > surf_w) sw = surf_w - sx;
    if (sy + sh > surf_h) sh = surf_h - sy;
    if (sw <= 0 or sh <= 0) return;

    const row_w: usize = @as(usize, @intCast(surf.width));
    var j: i32 = 0;
    while (j < sh) : (j += 1) {
        const row = @as(usize, @intCast(sy + j)) * row_w;
        var i: i32 = 0;
        while (i < sw) : (i += 1) {
            const idx = row + @as(usize, @intCast(sx + i));
            surf.pixels[idx] = color;
        }
    }
}

fn rectVGradient(surf: *surface_t, x: i32, y: i32, w: i32, h: i32, top: u32, bottom: u32) void {
    if (w <= 0 or h <= 0) return;
    var sx = x;
    var sy = y;
    var sw = w;
    var sh = h;
    const surf_w: i32 = @as(i32, @intCast(surf.width));
    const surf_h: i32 = @as(i32, @intCast(surf.height));
    if (sx < 0) {
        sw += sx;
        sx = 0;
    }
    if (sy < 0) {
        sh += sy;
        sy = 0;
    }
    if (sx + sw > surf_w) sw = surf_w - sx;
    if (sy + sh > surf_h) sh = surf_h - sy;
    if (sw <= 0 or sh <= 0) return;

    const a1: u32 = (top >> 24) & 0xFF;
    const r1: u32 = (top >> 16) & 0xFF;
    const g1: u32 = (top >> 8) & 0xFF;
    const b1: u32 = top & 0xFF;
    const a2: u32 = (bottom >> 24) & 0xFF;
    const r2: u32 = (bottom >> 16) & 0xFF;
    const g2: u32 = (bottom >> 8) & 0xFF;
    const b2: u32 = bottom & 0xFF;

    const denom: i32 = if (sh > 1) sh - 1 else 1;
    const row_w: usize = @as(usize, @intCast(surf.width));

    var j: i32 = 0;
    while (j < sh) : (j += 1) {
        const t: i32 = @divTrunc(j * 255, denom);
        const a: u32 = @as(u32, @intCast(@as(i32, @intCast(a1)) + @divTrunc((@as(i32, @intCast(a2)) - @as(i32, @intCast(a1))) * t, 255)));
        const r: u32 = @as(u32, @intCast(@as(i32, @intCast(r1)) + @divTrunc((@as(i32, @intCast(r2)) - @as(i32, @intCast(r1))) * t, 255)));
        const g: u32 = @as(u32, @intCast(@as(i32, @intCast(g1)) + @divTrunc((@as(i32, @intCast(g2)) - @as(i32, @intCast(g1))) * t, 255)));
        const b: u32 = @as(u32, @intCast(@as(i32, @intCast(b1)) + @divTrunc((@as(i32, @intCast(b2)) - @as(i32, @intCast(b1))) * t, 255)));
        const color: u32 = (a << 24) | (r << 16) | (g << 8) | b;
        const row = @as(usize, @intCast(sy + j)) * row_w;
        var i: i32 = 0;
        while (i < sw) : (i += 1) {
            const idx = row + @as(usize, @intCast(sx + i));
            surf.pixels[idx] = color;
        }
    }
}

export fn wm_chrome_draw_zig(
    surf: *surface_t,
    x: i32,
    y: i32,
    w: i32,
    title_h: i32,
    title_bg: u32,
) callconv(.c) void {
    @setRuntimeSafety(false);
    if (@intFromPtr(surf.pixels) == 0) return;
    if (surf.width == 0 or surf.height == 0) return;
    if (surf.width > 8192 or surf.height > 8192) return;
    if (w <= 0 or title_h <= 0) return;

    const top = shadeColor(title_bg, 24);
    const bot = shadeColor(title_bg, -18);
    rectVGradient(surf, x, y, w, title_h, top, bot);
    rectFill(surf, x, y + title_h, w, 1, shadeColor(title_bg, -40));
}
