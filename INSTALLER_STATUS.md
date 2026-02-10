# Chrysalis OS - Installer Development Status

## 🚀 Current Status: STABLE (v2.0)

The installer has evolved from a simple Python script/simulated environment into a **Standalone C++ mini-OS** that runs directly from a bootable ISO.

### ✅ Completed Milestones

- **Standalone Bootability**: Installer runs in its own Multiboot2 environment (VGA Text Mode).
- **Disk I/O**: Full ATA/IDE driver support with LBA 28/48 addressing.
- **FS Portability**: Real-time FAT32 formatting and file copying from ISO modules to Disk.
- **Upgrade Logic**:
  - Intelligent detection of existing `/boot/chrysalis/kernel.bin`.
  - Preserves `/system/` user data while updating binary assets.
  - Handles directory existence errors gracefully.
- **Bug Fixes**:
  - **Incomplete Write Fix**: Refactored FAT32 write logic to prevent BPB corruption during large file writes (e.g., icons).
  - **Stability**: Added `kmalloc_reset()` to prevent OOM during batch icon installation.
  - **Multitasking Support**: Installer now runs in full text mode by disabling the framebuffer tag in Multiboot2.

---

## 🛠️ Internal Architecture

### 1. Boot Sequence

- GRUB loads `installer.elf`.
- OS kernel modules are passed as Multiboot modules (`kernel.bin`, `icons`, `grub assets`).
- Installer maps these modules in memory to source data for the installation.

### 2. FAT32 Driver

- Handles cluster allocation and LFN entry creation.
- **Verified Writes**: Every block written can be verified against the source buffer.
- **BPB Protection**: Critical filesystem parameters are cached in local variables to avoid corruption during sector buffer reuse.

### 3. VGA Stubs

- Custom `terminal_printf` and `serial` implementation for dual-output logging.
- Support for basic text UI (colors, scrolling, clear screen).

---

## 📉 Known Issues & Roadmap

### Refinements

- [x] Post-install summary screen.
- [x] Recovery shell for manual file management.
- [ ] Support for GPT/UEFI partitioning (currently MBR/BIOS only).
- [ ] Progress bar visualization (currently text-based progress).

### Stability

- FAT32 driver occasionally needs a `sync` or `cache flush` on some older ATA controllers.
- Keyboard layout is currently fixed to US (kernel supports RO, but installer stub is simplified).

---

## ⚙️ Build Info

- **Target**: i386-pc
- **Format**: ELF (Multiboot2)
- **Compiler**: G++ (Freestanding)
- **Linker**: LD (Linker script shared with main OS)

---

**Last Verified Build:** February 10, 2026
