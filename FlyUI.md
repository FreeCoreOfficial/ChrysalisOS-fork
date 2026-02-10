# 🦋 FlyUI — Chrysalis OS Graphical Engine

## Overview

FlyUI is the custom graphical engine and widget toolkit for Chrysalis OS. It powers the v0.2 desktop experience, providing windows, icons, and interactive elements.

## 🧱 Architecture

FlyUI follows a clean layered approach:

1. **Compositor**: Blits pixels to the VESA/Multiboot framebuffer.
2. **WM (Window Manager)**: Manages window state, layering (Z-index), and focus.
3. **FlyUI Core**: The widget engine that draws buttons, bars, and icons into window surfaces.

---

## 🎨 Features in v0.2

- **Window Management**: Active window highlighting, movement, and close buttons.
- **Icon Support**: Native BMP loader for 32-bit RGBA icons.
- **Theme Engine**: Centralized color tokens in `theme.c`.
- **Event Bus**: Mouse clicks and keyboard focus redirection to the active window.
- **Terminal Integration**: `Konsole` app allows running the shell inside a window.

---

## 💻 Modding & Development

### Adding a Widget

Widgets are defined in `os/kernel/ui/flyui/`. To create a custom widget, implement the `draw` and `on_event` handlers.

### Example: Creating a Button

```c
fly_draw_rect_fill(surf, x, y, w, h, theme->btn_bg);
fly_draw_text(surf, x + padding, y + padding, "Action", theme->btn_text);
```

### Changing Colors

The entire system's look can be changed in `os/kernel/ui/flyui/theme.c`.

```c
static fly_theme_t default_theme = {
    .win_bg = 0x1A1A1A,
    .win_title_active_bg = 0x005A9E,
    // ...
};
```

---

## 📂 Key Files

- `os/kernel/ui/wm/`: Window Manager and Layering.
- `os/kernel/ui/flyui/draw.c`: Primitives (Rects, Text, Bitmaps).
- `os/kernel/ui/flyui/bmp.c`: BMP decoder for icons.
- `os/kernel/ui/flyui/theme.c`: Color definitions.

---

## ⚠️ Known Limitations

- No alpha-blending (transparency) supporting yet.
- Windows are currently fixed-size after creation.
- Soft-rendering only (no hardware acceleration).

---

**Last Updated:** February 10, 2026
