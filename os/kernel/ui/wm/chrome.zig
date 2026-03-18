const c = @cImport({
    @cInclude("ui/flyui/draw.h");
});

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

export fn wm_chrome_draw_zig(
    surf: *c.surface_t,
    x: i32,
    y: i32,
    w: i32,
    title_h: i32,
    title_bg: u32,
) callconv(.c) void {
    if (w <= 0 or title_h <= 0) return;
    const top = shadeColor(title_bg, 24);
    const bot = shadeColor(title_bg, -18);
    c.fly_draw_rect_vgradient(surf, x, y, w, title_h, top, bot);
    c.fly_draw_rect_fill(surf, x, y + title_h, w, 1, shadeColor(title_bg, -40));
}
