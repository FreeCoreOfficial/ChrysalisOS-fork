# Chrysalis OS Standalone Installer

## Overview

Chrysalis OS v0.2 features a **Standalone Bootable Installer**. Unlike previous versions that required a command-prompt installation, the new installer is a dedicated mini-OS environment designed to prepare your hardware for Chrysalis OS.

## 🚀 Key Features

- **Standalone Boot:** Boots directly from `installer.iso`.
- **Dual Mode:**
  - **Fresh Install:** Formats the target disk (LBA 0), creates a FAT32 filesystem, and installs a clean copy of the OS.
  - **Upgrade:** Detects an existing Chrysalis OS installation, preserves user data, and only updates kernel/system files.
- **Disk Management:** Automated ATA disk detection and partitioning.
- **Visual Interface:** Classic "Blue Screen" text console with progress bars and status updates.
- **Post-Install Actions:** Choice of Reboot, Shutdown, or dropping into a Recovery Shell.

---

## 🛠️ Usage Instructions

### 1. Preparing the Media

Ensure you have built the system and generated the installer image:

```sh
cd os && npm run build
cd os && npm run create-i
```

This produces `installer.iso`.

### 2. Booting the Installer

Boot your virtual machine (QEMU/VMWare) or physical hardware using the ISO.

- QEMU: `qemu-system-i386 -cdrom installer.iso -m 256M`

### 3. Installation Steps

1. **Welcome Screen:** Select between `[1] Fresh Install` or `[2] Upgrade`.
2. **Setup:** The installer scans for ATA disks and partitions.
3. **Copying:** All necessary assets (Kernel, Icons, Bootloader) are copied to `/boot` and `/system`.
4. **Completion:** Review the summary and choose your exit action.

---

## 📂 File Structure on Target Disk

After a successful installation, your disk will have the following layout:

```
/
├── boot/
│   ├── grub/
│   │   ├── grub.cfg    # Bootloader config
│   │   ├── boot.img    # GRUB stage 1
│   │   └── core.img    # GRUB stage 2
│   └── chrysalis/
│       └── kernel.bin  # The main OS Kernel
└── system/
    └── icons/          # BMP Icons for FlyUI
        ├── start.bmp
        ├── term.bmp
        └── ...
```

---

## 🔧 Troubleshooting

### Black Screen on Boot

The installer uses legacy VGA text mode (`0xB8000`). If you see a black screen, ensure your emulator supports standard VGA and that the `multiboot2_header` isn't forcing a graphics mode (this is handled automatically by the installer's `NO_FRAMEBUFFER` flag).

### Write Failures

If the installer reports "Incomplete write" or "Disk full":

1. Ensure the target disk is at least 512MB.
2. If upgrading, run `fat` command in the OS to check fragmentation.

### Recovery Shell

If installation fails or you need manual intervention, choose `[S]` at the success screen.
Available commands: `help`, `reboot`, `version`, `exit`.

---

**Version:** 2.0 (Standalone)  
**Last Updated:** February 10, 2026
