# Chrysalis OS – Build Guide (v0.2)

This document describes how to build **Chrysalis OS** from source.
Chrysalis OS v0.2 uses a modular structure with a separate OS kernel and a Standalone Installer.

---

## Supported Host Systems

Recommended:

- Linux (Debian / Ubuntu / Arch / Fedora)
- Windows (via WSL2 / Ubuntu)

---

## Required Dependencies

### 1. Core Build Tools

```sh
sudo apt install build-essential gcc-multilib g++-multilib \
                 nasm make xorriso grub-pc-bin \
                 qemu-system-i386 mtools
```

### 2. Node.js (Build Wrapper)

The build system uses `npm` scripts for high-level management.

```sh
# Install Node.js & NPM if not present
sudo apt install nodejs npm
```

---

## Directory Structure (v0.2)

```
ChrysalisOS/
├── os/                  # Main OS Kernel & Drivers
│   ├── kernel/          # Kernel Source
│   ├── arch/            # Architecture specific (x86)
│   ├── build/           # Build output
│   └── Makefile         # Kernel Makefile
├── installer/           # Standalone Installer
│   ├── installer.cpp    # Installer logic
│   ├── stubs.cpp        # VGA Text Mode stubs
│   └── Makefile         # Installer Makefile
└── tools/               # Asset generation
```

---

## Build Commands

From the **project root**:

### Build Everything

```sh
cd os
npm run build        # Builds both Kernel and Installer
```

### Build Only Kernel

```sh
cd os
make
```

### Build Installer ISO

```sh
cd os
npm run create-i     # Generates installer.iso
```

---

## Running and Testing

### Quick Start (QEMU)

```sh
cd os
npm run boot         # Boots the OS from virtual HDD
```

### Test Installer

```sh
cd os
npm run installer:log # Boots the Installer ISO and logs to serial
```

Manual QEMU command:

```sh
qemu-system-i386 -cdrom installer.iso -serial stdio -m 256M
```

---

## Build Options

### Debug Symbols

To enable GDB symbols and serial debugging:

```sh
make DEBUG=1
```

### Skip Cache Flush (ATA)

If testing on slow storage emulators, the ATA driver can be tuned in `os/kernel/drivers/ata.cpp`.

---

## Common Issues

### `installer.iso` not found

Ensure you ran `npm run create-i` inside the `os` directory. Detailed logs of the ISO creation are available in `os/installer_build.log`.

### Black Screen on Boot

If the installer shows a black screen, ensure the `multiboot2_header.c` was compiled with `-DNO_FRAMEBUFFER` (automatic in the installer Makefile).

---

## License

Chrysalis OS is released under the **GNU GPL v3**.

Happy building!
