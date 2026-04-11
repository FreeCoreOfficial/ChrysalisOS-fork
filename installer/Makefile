# Minimal Chrysalis OS Installer Makefile
# Purpose: Build installer ISO with setup wizard

CC = gcc
CXX = g++
AS = nasm
LD = ld

CHRYVER := $(shell sed -n 's/^CHRYVER= *\"\\(.*\\)\".*/\\1/p' ../os/Makefile)

CFLAGS = -fno-builtin -Wall -Wextra -ffreestanding -O2 -m32 -I../os/kernel/include -I../os/kernel -DNO_FRAMEBUFFER -DINSTALLER_BUILD
CXXFLAGS = $(CFLAGS) -fno-exceptions -fno-rtti
ifneq ($(strip $(CHRYVER)),)
CFLAGS += -DCHRYVER=\"$(CHRYVER)\"
CXXFLAGS += -DCHRYVER=\"$(CHRYVER)\"
endif
ASFLAGS = -f elf32
LDFLAGS = -nostdlib -m elf_i386 -T ../os/linker.ld

BUILD = build
ISO = iso
TCC_REQUIRED ?= 1

# Minimal object files (installer only)
# Minimal object files + OS Drivers
OBJS = \
	$(BUILD)/multiboot2_header.o \
	$(BUILD)/boot.o \
	$(BUILD)/kernel_installer.o \
	$(BUILD)/installer.o \
	$(BUILD)/disk_shim.o \
	$(BUILD)/ata_installer.o \
	$(BUILD)/kmalloc_simple.o \
	$(BUILD)/partitions_stub.o \
	$(BUILD)/stubs.o \
	../os/build/fat_fs.o \
	../os/build/cmd_fat.o \
	../os/build/serial.o \
	../os/build/ramfs.o \
	../os/build/ramfs_add.o \
	../os/build/input/input.o \
	../os/build/string.o \
	$(BUILD)/usb_core.o \
	$(BUILD)/uhci.o \
	$(BUILD)/ehci.o \
	$(BUILD)/xhci.o \
	$(BUILD)/usb_hid.o \
	$(BUILD)/usb_hub.o \
	$(BUILD)/usb_msc.o \
	$(BUILD)/cmd_ls.o \
	$(BUILD)/cmd_cd.o \
	$(BUILD)/cmd_reboot.o \
	$(BUILD)/cmd_shutdown.o \
	$(BUILD)/sha256.o

# Force looking for headers in OS include
CFLAGS += -I../os/kernel/include

all: $(BUILD) installer.iso

$(BUILD):
	@mkdir -p $(BUILD) $(ISO)/boot/grub $(ISO)/boot/chrysalis $(ISO)/system

# Bootloader entry point
$(BUILD)/boot.o: ../os/kernel/boot.asm
	@mkdir -p $(BUILD)
	@echo "[BUILD] boot.asm"
	$(AS) $(ASFLAGS) $< -o $@

# Multiboot2 header
$(BUILD)/multiboot2_header.o: ../os/arch/i386/multiboot2_header.c
	@mkdir -p $(BUILD)
	@echo "[BUILD] multiboot2_header.c"
	$(CC) $(CFLAGS) -c $< -o $@

# Kernel entry point and installer implementation
$(BUILD)/kernel_installer.o: kernel.cpp
	@mkdir -p $(BUILD)
	@echo "[BUILD] kernel.cpp (kernel entry + installer)"
	$(CXX) $(CXXFLAGS) -c $< -o $@

# Real installer implementation
$(BUILD)/installer.o: installer.cpp
	@mkdir -p $(BUILD)
	@echo "[BUILD] installer.cpp (real installation)"
	$(CXX) $(CXXFLAGS) -c $< -o $@

# Disk shim
$(BUILD)/disk_shim.o: disk_shim.cpp
	@mkdir -p $(BUILD)
	@echo "[BUILD] disk_shim.cpp"
	$(CXX) $(CXXFLAGS) -c $< -o $@

# ATA driver (installer-specific: serial-only logs, no VGA spam)
$(BUILD)/ata_installer.o: ../os/kernel/storage/ata.c
	@mkdir -p $(BUILD)
	@echo "[BUILD] ata.c (installer serial-only logs)"
	$(CC) $(CFLAGS) -DATA_LOG_SERIAL_ONLY=1 -c $< -o $@

# USB stack (installer build)
$(BUILD)/usb_core.o: ../os/kernel/usb/usb_core.c
	@mkdir -p $(BUILD)
	@echo "[BUILD] usb_core.c"
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD)/uhci.o: ../os/kernel/usb/uhci.c
	@mkdir -p $(BUILD)
	@echo "[BUILD] uhci.c"
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD)/ehci.o: ../os/kernel/usb/ehci.c
	@mkdir -p $(BUILD)
	@echo "[BUILD] ehci.c"
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD)/xhci.o: ../os/kernel/usb/xhci.c
	@mkdir -p $(BUILD)
	@echo "[BUILD] xhci.c"
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD)/usb_hid.o: ../os/kernel/usb/usb_hid.c
	@mkdir -p $(BUILD)
	@echo "[BUILD] usb_hid.c"
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD)/usb_hub.o: ../os/kernel/usb/usb_hub.c
	@mkdir -p $(BUILD)
	@echo "[BUILD] usb_hub.c"
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD)/usb_msc.o: ../os/kernel/usb/usb_msc.c
	@mkdir -p $(BUILD)
	@echo "[BUILD] usb_msc.c"
	$(CC) $(CFLAGS) -c $< -o $@

# Simple kmalloc for installer (no PMM)
$(BUILD)/kmalloc_simple.o: kmalloc_simple.cpp
	@mkdir -p $(BUILD)
	@echo "[BUILD] kmalloc_simple.cpp"
	$(CXX) $(CXXFLAGS) -c $< -o $@

# Partitions stub
$(BUILD)/partitions_stub.o: partitions_stub.cpp
	@mkdir -p $(BUILD)
	@echo "[BUILD] partitions_stub.cpp"
	$(CXX) $(CXXFLAGS) -c $< -o $@

# Stubs for missing kernel symbols
$(BUILD)/stubs.o: stubs.cpp
	@mkdir -p $(BUILD)
	@echo "[BUILD] stubs.cpp"
	$(CXX) $(CXXFLAGS) -c $< -o $@

# Local build of kernel commands for installer
$(BUILD)/cmd_ls.o: ../os/kernel/cmds/ls.cpp
	@mkdir -p $(BUILD)
	@echo "[BUILD] cmd_ls.o"
	$(CXX) $(CXXFLAGS) -c $< -o $@

$(BUILD)/cmd_cd.o: ../os/kernel/cmds/cd.cpp
	@mkdir -p $(BUILD)
	@echo "[BUILD] cmd_cd.o"
	$(CXX) $(CXXFLAGS) -c $< -o $@

$(BUILD)/cmd_reboot.o: ../os/kernel/cmds/reboot.cpp
	@mkdir -p $(BUILD)
	@echo "[BUILD] cmd_reboot.o"
	$(CXX) $(CXXFLAGS) -c $< -o $@

$(BUILD)/cmd_shutdown.o: ../os/kernel/cmds/shutdown.cpp
	@mkdir -p $(BUILD)
	@echo "[BUILD] cmd_shutdown.o"
	$(CXX) $(CXXFLAGS) -c $< -o $@

# SHA256 implementation
$(BUILD)/sha256.o: ../os/kernel/crypto/sha256.c
	@mkdir -p $(BUILD)
	@echo "[BUILD] sha256.c"
	$(CC) $(CFLAGS) -c $< -o $@

# Kernel ELF binary
$(BUILD)/installer.elf: $(OBJS)
	@echo "[LINK] Creating installer.elf"
	$(LD) $(LDFLAGS) $(OBJS) -o $@

# ISO creation
installer.iso: $(BUILD)/installer.elf
	@# Always rebuild OS ISO so kernel64/modules stay in sync with installer.
	@$(MAKE) -C ../os iso
	@echo "[ISO] Preparing assets..."
	@mkdir -p $(ISO)/boot/chrysalis
	@mkdir -p $(ISO)/boot/grub
	@cp $(BUILD)/installer.elf $(ISO)/boot/
	@# Copy Assets from Main OS Build
	@if [ -f ../os/iso/boot/kernel.bin ]; then cp ../os/iso/boot/kernel.bin $(ISO)/boot/chrysalis/; else echo "WARNING: kernel.bin not found!"; fi
	@if [ -f ../os/iso/boot/kernel64.bin ]; then cp ../os/iso/boot/kernel64.bin $(ISO)/boot/chrysalis/; else echo "WARNING: kernel64.bin not found!"; fi
	@if [ -f ../os/iso/boot/hello64.elf ]; then cp ../os/iso/boot/hello64.elf $(ISO)/boot/chrysalis/; else echo "WARNING: hello64.elf not found!"; fi
	@mkdir -p $(ISO)/system/tests
	@if [ -d ../os/iso/system/tests ]; then cp ../os/iso/system/tests/* $(ISO)/system/tests/ 2>/dev/null || true; else echo "WARNING: system tests not found!"; fi
	@mkdir -p $(ISO)/system/bin
	@if [ -f ../os/iso/system/bin/tcc ]; then \
		cp ../os/iso/system/bin/tcc $(ISO)/system/bin/; \
		echo "[ISO] TinyCC copied to installer ISO"; \
	else \
		echo "WARNING: tcc not found in os/iso/system/bin (run yarn build in os/)"; \
		if [ "$(TCC_REQUIRED)" = "1" ]; then \
			echo "[ISO] TinyCC is required (set TCC_REQUIRED=0 to bypass)."; \
			exit 1; \
		fi; \
	fi
	@mkdir -p $(ISO)/system/config
	@cp ./config/*.conf $(ISO)/system/config/ 2>/dev/null || true
	@mkdir -p $(ISO)/lib $(ISO)/lib64
	@cp -a ../os/iso/lib/* $(ISO)/lib/ 2>/dev/null || true
	@cp -a ../os/iso/lib64/* $(ISO)/lib64/ 2>/dev/null || true

	@mkdir -p $(ISO)/system
	@if [ -f ../os/iso/system/bg.bmp ]; then \
		cp ../os/iso/system/bg.bmp $(ISO)/system/bg.bmp; \
		echo "[ISO] Wallpaper bg.bmp copied to installer ISO"; \
	else echo "WARNING: bg.bmp not found in os/iso/system/ (run 'make icons' in os/ first!)"; fi
	@# Build GRUB core image + include boot.img + embed config
	@if [ -f /usr/lib/grub/i386-pc/boot.img ]; then cp /usr/lib/grub/i386-pc/boot.img $(ISO)/boot/grub/; else echo "WARNING: boot.img not found!"; fi
	@echo 'search --file --set=root /boot/grub/grub.cfg' > $(ISO)/boot/grub/grub_embed.cfg
	@echo 'set prefix=($$root)/boot/grub' >> $(ISO)/boot/grub/grub_embed.cfg
	@echo 'configfile ($$root)/boot/grub/grub.cfg' >> $(ISO)/boot/grub/grub_embed.cfg
	@echo 'search --file --set=root /grub.cfg' >> $(ISO)/boot/grub/grub_embed.cfg
	@echo 'set prefix=($$root)/boot/grub' >> $(ISO)/boot/grub/grub_embed.cfg
	@echo 'configfile /grub.cfg' >> $(ISO)/boot/grub/grub_embed.cfg
	@grub-mkimage -O i386-pc -p /boot/grub -o $(ISO)/boot/grub/core.img -c $(ISO)/boot/grub/grub_embed.cfg biosdisk part_msdos fat normal multiboot2 configfile search search_fs_uuid search_label gfxterm gfxmenu png tga jpeg font all_video vbe vga
	@
	@echo "[ISO] Installing GRUB theme..."
	@mkdir -p $(ISO)/boot/grub/themes/chrysalis
	@if [ -f ../os/iso/boot/grub/themes/chrysalis/theme.txt ]; then \
		echo "[ISO] Using OS GRUB theme"; \
		cp ../os/iso/boot/grub/themes/chrysalis/* $(ISO)/boot/grub/themes/chrysalis/ 2>/dev/null || true; \
	else \
		echo "[ISO] Using bundled GRUB theme"; \
		cp ../grub-theme/* $(ISO)/boot/grub/themes/chrysalis/ 2>/dev/null || true; \
	fi
	@echo "[ISO] Generating GRUB config..."
	@echo 'set theme=/boot/grub/themes/chrysalis/theme.txt' > $(ISO)/boot/grub/grub.cfg
	@echo 'set timeout=5' >> $(ISO)/boot/grub/grub.cfg
	@echo 'set default=0' >> $(ISO)/boot/grub/grub.cfg
	@echo '' >> $(ISO)/boot/grub/grub.cfg
	@echo 'menuentry "Chrysalis OS - Installer" {' >> $(ISO)/boot/grub/grub.cfg
	@echo '  multiboot2 /boot/installer.elf' >> $(ISO)/boot/grub/grub.cfg
	@echo '  module2 /boot/chrysalis/kernel.bin kernel.bin' >> $(ISO)/boot/grub/grub.cfg
	@if [ -f $(ISO)/boot/chrysalis/kernel64.bin ]; then \
		echo '  module2 /boot/chrysalis/kernel64.bin kernel64.bin' >> $(ISO)/boot/grub/grub.cfg; \
	fi
	@if [ -f $(ISO)/boot/chrysalis/hello64.elf ]; then \
		echo '  module2 /boot/chrysalis/hello64.elf hello64.elf' >> $(ISO)/boot/grub/grub.cfg; \
	fi
	@for f in $(ISO)/lib/*; do \
		if [ -f $$f ]; then \
			nm=$$(basename $$f); \
			echo "  module2 /lib/$$nm $$nm" >> $(ISO)/boot/grub/grub.cfg; \
		fi \
	done
	@for f in $(ISO)/lib64/*; do \
		if [ -f $$f ]; then \
			nm=$$(basename $$f); \
			echo "  module2 /lib64/$$nm $$nm" >> $(ISO)/boot/grub/grub.cfg; \
		fi \
	done

	@for f in $(ISO)/system/tests/*; do \
		nm=$$(basename $$f); \
		echo "  module2 /system/tests/$$nm $$nm" >> $(ISO)/boot/grub/grub.cfg; \
	done
	@for f in $(ISO)/system/bin/*; do \
		if [ -f $$f ]; then \
			nm=$$(basename $$f); \
			echo "  module2 /system/bin/$$nm $$nm" >> $(ISO)/boot/grub/grub.cfg; \
		fi \
	done
	@for f in $(ISO)/system/config/*.conf; do \
		if [ -f $$f ]; then \
			nm=$$(basename $$f); \
			echo "  module2 /system/config/$$nm $$nm" >> $(ISO)/boot/grub/grub.cfg; \
		fi \
	done
	@if [ -f $(ISO)/system/bg.bmp ]; then \
		echo '  module2 /system/bg.bmp bg.bmp' >> $(ISO)/boot/grub/grub.cfg; \
	fi
	@echo '  module2 /boot/grub/boot.img boot.img' >> $(ISO)/boot/grub/grub.cfg
	@echo '  module2 /boot/grub/core.img core.img' >> $(ISO)/boot/grub/grub.cfg
	@echo '  module2 /boot/grub/themes/chrysalis/background.tga background.tga' >> $(ISO)/boot/grub/grub.cfg
	@echo '  module2 /boot/grub/themes/chrysalis/select_c.tga select_c.tga' >> $(ISO)/boot/grub/grub.cfg
	@echo '  module2 /boot/grub/themes/chrysalis/theme.txt theme.txt' >> $(ISO)/boot/grub/grub.cfg
	@echo '  boot' >> $(ISO)/boot/grub/grub.cfg
	@echo '}' >> $(ISO)/boot/grub/grub.cfg
	@# Add graphical configuration at the beginning
	@echo "Prepending graphical config to grub.cfg..."
	@(echo '# Load graphical modules'; \
	 echo 'insmod png'; \
	 echo 'insmod tga'; \
	 echo 'insmod jpeg'; \
	 echo 'insmod font'; \
	 echo 'insmod gfxterm'; \
	 echo 'insmod vbe'; \
	 echo 'insmod vga'; \
	 echo ''; \
	 echo '# Set graphics mode'; \
	 echo 'set gfxmode=1024x768,800x600,auto'; \
	 echo 'set gfxpayload=keep'; \
	 echo ''; \
	 echo '# Enable graphical terminal'; \
	 echo 'terminal_output gfxterm'; \
	 echo ''; \
	 cat $(ISO)/boot/grub/grub.cfg) > $(ISO)/boot/grub/grub.cfg.tmp
	@mv $(ISO)/boot/grub/grub.cfg.tmp $(ISO)/boot/grub/grub.cfg
	@
	@echo "[ISO] Generating ISO image..."
	@GRUB_FONT_PATH=/usr/share/grub grub-mkrescue -o installer.iso --modules="gfxterm gfxmenu png tga jpeg font all_video" $(ISO) 2>&1 | grep -v "xorriso:"
	@ls -lh installer.iso | awk '{print "[ISO] Created: " $$9 " (" $$5 " bytes)"}'

clean:
	@echo "[CLEAN] Removing build artifacts..."
	@rm -rf $(BUILD) $(ISO) installer.iso

.PHONY: all clean
