# Chrysalis OS – Modding Guide (v0.2)

Chrysalis OS is designed from the ground up to be **hackable, moddable, and fork-friendly**. This document explains how to extend the system, from the kernel core to the graphical interface.

---

## 🛠️ Kernel Modding: Adding a Command

Chrysalis OS uses a registry-based system for shell commands. To add a new command:

1. Create your command function in `os/kernel/cmds/`.
2. Register it in `os/kernel/cmds/registry.cpp`.

### Example Snippet: `hello` command

```cpp
// 1. In a new file or existing cmd file:
int cmd_hello(int argc, char **argv) {
    terminal_printf("Hello from the kernel, %s!\n", argc > 1 ? argv[1] : "Chrysalis User");
    return 0;
}

// 2. Wrap it in registry.cpp:
static int wrap_cmd_hello(int argc, char **argv) {
    return cmd_hello(argc, argv);
}

// 3. Add to command_table[]:
Command command_table[] = {
    ...
    {"hello", wrap_cmd_hello},
    ...
};
```

---

## 🔌 System Calls: Extending the API

Syscalls allow user-mode programs (like `.petal` apps) to talk to the kernel.

1. Add a new ID in `os/kernel/include/chrysalis/syscall_nums.h`.
2. Implement the logic in `os/kernel/arch/i386/syscall_dispatch.cpp`.

### Example Snippet: `SYS_GET_TICKS`

```cpp
// 1. syscall_nums.h
#define SYS_GET_TICKS 40

// 2. syscall_dispatch.cpp
case SYS_GET_TICKS:
    return (int)timer_get_ticks();
```

---

## 🎨 UI Modding: FlyUI Widgets & Apps

The **FlyUI** framework (in `os/kernel/ui/flyui/`) is used for all graphical elements.

### Creating a Simple Window

```cpp
void my_app_init() {
    surface_t* surf = surface_create(300, 200);
    window_t* win = wm_create_window(surf, 100, 100);
    wm_set_title(win, "My Modded App");
    
    // Draw a button
    fly_draw_rect_fill(surf, 10, 10, 80, 30, color_grey);
    fly_draw_text(surf, 15, 15, "Click Me", color_white);
    
    wm_mark_dirty();
}
```

### Changing Themes

Themes are strictly defined in `os/kernel/ui/flyui/theme.c`. You can modify the `default_theme` structure to change the entire system's look (colors for title bars, backgrounds, and text).

---

## 💿 Installer Modding

The installer is a separate binary located in `/installer`. It uses a custom **Blue Screen** text UI.

- Modifying `installer/installer.cpp` allows you to change the installation routine (partitioning, file copying).
- Modifying `installer/stubs.cpp` changes the low-level VGA text output behavior.

---

## 🛡️ Best Practices

- **Serial Logs**: Use `serial("[TAG] Message\n")` for debugging. It works even when the VGA/GUI crashes.
- **Memory**: Always use `kmalloc`/`kfree` for dynamic objects.
- **Header Alingment**: If you modify `multiboot2_header.c`, ensure structures are packed and 8-byte aligned.

---

## Final Note

If you break it — you learned something.  
If you improve it — share it.  
**Happy hacking!**
