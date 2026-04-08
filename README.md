# Chrysalis OS ![License: MIT](https://img.shields.io/badge/License-MIT-yellow.svg) | <iframe src="https://github.com/sponsors/mihai209/button" title="Sponsor mihai209" height="32" width="114" style="border: 0; border-radius: 6px;"></iframe> |  [![ko-fi](https://ko-fi.com/img/githubbutton_sm.svg)](https://ko-fi.com/S6S11A22SE)

Chrysalis OS is a custom operating system built from scratch in **C/C++**, designed as a
hands-on learning and research project in low-level systems programming.
It has evolved from a minimal terminal-based system into a graphical operating system with its own UI framework (**FlyUI**), a standalone installer, and support for standalone ELF executables.

---

## 🦋 Why “Chrysalis”?

The name **Chrysalis** was chosen to reflect the core idea behind the project: **transformation**.

Just like a chrysalis represents the transitional stage before a butterfly emerges, Chrysalis OS began as a minimal bootloader/kernel project and is continuously evolving into a feature-rich operating system.

The name symbolizes:
- gradual growth
- continuous learning
- evolution from simplicity to complexity

---

## Project Overview

- **Name:** Chrysalis OS  
- **Version:** 0.2 "Ethereal"
- **Website:** [https://chrysalisos.netlify.app](https://chrysalisos.netlify.app)
- **Language:** C/C++ with x86 Assembly  
- **Architecture:** x86 (i386)  
- **Bootloader:** GRUB (Multiboot2 support)  
- **Filesystem:** FAT32 (Full R/W support)
- **UI Framework:** FlyUI (Custom C-based graphical engine)
- **Testing:** QEMU / VirtualBox / Real Hardware

---

## Key Features (v0.2)

- **FlyUI Graphical Interface:** A custom window manager and widget toolkit supporting icons, windows, and mouse interaction.
- **Standalone Installer:** A dedicated bootable installer with "Fresh Install" and "Upgrade" modes, featuring a classic "Blue Screen" text interface.
- **FAT32 Driver:** High-performance filesystem driver with support for long file names (LFN) and verified writes.
- **ELF Program Support:** Capability to run standalone programs (`.petal`) with a dedicated syscall interface.
- **Multiboot2 Booting:** Modern booting standard support, handling memory maps and framebuffer information.
- **Driver Support:** ATA/IDE, Keyboard (US/RO), Serial logging, RTC, CMOS, PIT, and PC Speaker.

---

## Project Structure

```
ChrysalisOS/
├── os/                  # Main OS Source Code
│   ├── kernel/          # Core Kernel (Memory, Tasks, Syscalls)
│   │   ├── drivers/     # Hardware Drivers (ATA, Keyboard, VGA, Serial)
│   │   ├── fs/          # Filesystems (FAT32, RamFS, VFS)
│   │   ├── ui/          # FlyUI Graphical Engine & Compositor
│   │   └── apps/        # Built-in Kernel Apps
│   └── arch/i386/       # X86 Specific Boot & Header code
├── installer/           # Standalone Installer Source
│   ├── installer.cpp    # Installation Logic (Formatter, File Copier)
│   └── stubs.cpp        # VGA Text Mode Console & Shims
├── tools/               # Build & Asset Tools (Icon generators, HDD tools)
└── README.md            # You are here
```

---

## Development Workflow

1. **Build the OS:** `cd os && npm run build`
2. **Create Installer Media:** `cd os && npm run create-i`
3. **Test in QEMU:** `cd os && npm run boot` or `npm run installer:log`

---

## Resources & Documentation

* **FlyUI Documentation:** `FlyUI.md`
* **Installer Details:** `INSTALLER.md`
* **OSDev Wiki:** [https://wiki.osdev.org/](https://wiki.osdev.org/)

---

## License

MIT License (see `LICENSE` file).

---

**Project Status:** Active Development (Alpha)
**Current Version:** 0.2
**Project Started:** December 31, 2025
**Last Updated:** February 10, 2026
