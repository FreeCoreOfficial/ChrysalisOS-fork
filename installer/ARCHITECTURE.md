# ChrysalisOS Installer Architecture

## Overview
The installer is a native ChrysalisOS setup environment that boots from
`installer.iso` and runs a guided text-mode wizard. The code is organized as:

1. Setup UI and orchestration in `installer.cpp`
2. Multiboot entry in `kernel.cpp`
3. Disk IO adapter in `disk_shim.cpp`
4. Installation assets bundled under `installer/iso`

The current workflow is:
1. Welcome screen
2. Disk and partition review
3. Target selection
4. Preflight validation
5. Installation summary
6. Explicit confirmation
7. Installation progress
8. Success or failure handling

## Directory Structure

```text
/installer/
├── installer.cpp         # Wizard flow + install orchestration
├── kernel.cpp            # Multiboot2 entry point
├── disk_shim.cpp         # ATA-backed sector read/write shim
├── Makefile              # Installer ELF + ISO build
├── build/                # Intermediate objects and installer ELF
└── iso/                  # Installation media payload
    ├── boot/grub/        # GRUB bootloader assets
    ├── boot/chrysalis/   # Kernel payloads
    ├── system/           # Wallpaper and system resources
    ├── lib/ + lib64/     # Shared libraries copied from the OS ISO
    └── icons/            # Icon modules copied onto the target system
```

## Installation Stages

### Stage 1: Detection
- Scan the MBR partition table
- Detect likely Chrysalis, Windows, Linux, and EFI partitions
- Select a target partition and infer the current boot strategy

### Stage 2: Safety Gates
- Validate minimum target size
- Flag destructive fresh installs explicitly
- Show preflight warnings and an installation summary
- Require typed confirmation before any write begins

### Stage 3: Provisioning
- Fresh install: format the selected partition as FAT32
- Upgrade: validate and mount the existing filesystem
- Create `/boot`, `/boot/grub`, `/boot/chrysalis`, and `/system` directories

### Stage 4: Media Import
- Read the kernel and assets from installer multiboot modules
- Install icons, services, wallpapers, theme assets, and kernel payloads
- Generate `grub.cfg` on the target filesystem

### Stage 5: Bootloader Installation
- Write BIOS/MBR GRUB stage data
- Install `core.img` and GRUB configuration
- Copy GRUB theme assets to `/boot/grub/themes/chrysalis`

### Stage 6: Finalization
- Create initial user metadata on fresh install
- Verify key files such as `/boot/chrysalis/kernel.bin`
- Present reboot, shutdown, or recovery options

## Boot Strategy

```text
BIOS -> MBR boot code -> GRUB -> kernel.bin -> ChrysalisOS
```

Notes:
- The current boot install path is BIOS/MBR based.
- The wizard now exposes boot strategy and target context earlier, so hybrid
  BIOS+UEFI support can be added later without redesigning the flow again.

## Files Deployed

### Installation Media
1. `installer.elf` - multiboot entry and installer runtime
2. `kernel.bin` - primary ChrysalisOS kernel payload
3. `kernel64.bin` / `hello64.elf` - optional prototype payloads
4. `boot.img` + `core.img` - GRUB bootloader images
5. Theme, wallpaper, icon, service, and library modules

### Target Filesystem
```text
/boot/chrysalis/kernel.bin
/boot/grub/grub.cfg
/boot/grub/core.img
/boot/grub/themes/chrysalis/*
/system/bg.bmp
/system/icons/*
/system/services/*
/system/users/<name>/data.json
```

## Implementation Status

✅ Installer boots from ISO
✅ Guided wizard flow
✅ Partition scan and target selection
✅ Preflight, summary, and explicit confirmation
✅ FAT32 provisioning and staged file installation
✅ GRUB BIOS bootloader installation
⏳ Hybrid BIOS+UEFI boot installation
⏳ Broader hardware validation
