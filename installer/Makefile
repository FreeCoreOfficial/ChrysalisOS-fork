# Minimal Chrysalis OS Installer Makefile
# Purpose: Build installer ISO with setup wizard

CC = gcc
CXX = g++
AS = nasm
LD = ld

CFLAGS = -fno-builtin -Wall -Wextra -ffreestanding -O2 -m32 -I../os/kernel/include -DNO_FRAMEBUFFER
CXXFLAGS = $(CFLAGS) -fno-exceptions -fno-rtti
ASFLAGS = -f elf32
LDFLAGS = -nostdlib -m elf_i386 -T ../os/linker.ld

BUILD = build
ISO = iso

# Minimal object files (installer only)
# Minimal object files + OS Drivers
OBJS = \
	$(BUILD)/multiboot2_header.o \
	$(BUILD)/boot.o \
	$(BUILD)/kernel_installer.o \
	$(BUILD)/installer.o \
	$(BUILD)/disk_shim.o \
	$(BUILD)/kmalloc_simple.o \
	$(BUILD)/partitions_stub.o \
	$(BUILD)/stubs.o \
	../os/build/ata.o \
	../os/build/fat_fs.o \
	../os/build/cmd_fat.o \
	../os/build/serial.o \
	../os/build/ramfs.o \
	../os/build/string.o \
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
	@echo "[ISO] Preparing assets..."
	@cp $(BUILD)/installer.elf $(ISO)/boot/
	@# Copy Assets from Main OS Build
	@if [ -f ../os/iso/boot/kernel.bin ]; then cp ../os/iso/boot/kernel.bin $(ISO)/boot/chrysalis/; else echo "WARNING: kernel.bin not found!"; fi
	@mkdir -p $(ISO)/icons
	@if [ -d ../os/iso/icons ]; then cp ../os/iso/icons/*.bmp $(ISO)/icons/; else echo "WARNING: icons bmp not found!"; fi
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
	@cp ../grub-theme/* $(ISO)/boot/grub/themes/chrysalis/ 2>/dev/null || true
	@echo "[ISO] Generating GRUB config..."
	@echo 'set theme=/boot/grub/themes/chrysalis/theme.txt' > $(ISO)/boot/grub/grub.cfg
	@echo 'set timeout=5' >> $(ISO)/boot/grub/grub.cfg
	@echo 'set default=0' >> $(ISO)/boot/grub/grub.cfg
	@echo '' >> $(ISO)/boot/grub/grub.cfg
	@echo 'menuentry "Chrysalis OS - Installer" {' >> $(ISO)/boot/grub/grub.cfg
	@echo '  multiboot2 /boot/installer.elf' >> $(ISO)/boot/grub/grub.cfg
	@echo '  module2 /boot/chrysalis/kernel.bin kernel.bin' >> $(ISO)/boot/grub/grub.cfg
	@echo '  module2 /icons/start.bmp start.bmp' >> $(ISO)/boot/grub/grub.cfg
	@echo '  module2 /icons/term.bmp term.bmp' >> $(ISO)/boot/grub/grub.cfg
	@echo '  module2 /icons/files.bmp files.bmp' >> $(ISO)/boot/grub/grub.cfg
	@echo '  module2 /icons/img.bmp img.bmp' >> $(ISO)/boot/grub/grub.cfg
	@echo '  module2 /icons/note.bmp note.bmp' >> $(ISO)/boot/grub/grub.cfg
	@echo '  module2 /icons/paint.bmp paint.bmp' >> $(ISO)/boot/grub/grub.cfg
	@echo '  module2 /icons/calc.bmp calc.bmp' >> $(ISO)/boot/grub/grub.cfg
	@echo '  module2 /icons/clock.bmp clock.bmp' >> $(ISO)/boot/grub/grub.cfg
	@echo '  module2 /icons/task.bmp task.bmp' >> $(ISO)/boot/grub/grub.cfg
	@echo '  module2 /icons/info.bmp info.bmp' >> $(ISO)/boot/grub/grub.cfg
	@echo '  module2 /icons/3D.bmp 3D.bmp' >> $(ISO)/boot/grub/grub.cfg
	@echo '  module2 /icons/mine.bmp mine.bmp' >> $(ISO)/boot/grub/grub.cfg
	@echo '  module2 /icons/net.bmp net.bmp' >> $(ISO)/boot/grub/grub.cfg
	@echo '  module2 /icons/x0.bmp x0.bmp' >> $(ISO)/boot/grub/grub.cfg
	@echo '  module2 /icons/run.bmp run.bmp' >> $(ISO)/boot/grub/grub.cfg
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
