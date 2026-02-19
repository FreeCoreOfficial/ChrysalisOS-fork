KERNEL_CODENAME = "mosquito-with-violin"
LICENSE = "MIT"
CHRYVER= "chrysver-0.2.1"
GITHUB= "https://github.com/mihai209/chrysalisos"


CXX = g++
CC = gcc
AS = nasm
LD = ld

# flags comune
COMMON_FLAGS = -ffreestanding -O2 -Wall -Wextra -Ikernel/include -m32
# CFLAGS += -DSCHED_COOPERATIVE # uncomment = fallback safe mode

# C / C++
CFLAGS = $(COMMON_FLAGS) -DKERNEL_CODENAME=\"$(KERNEL_CODENAME)\" -DCHRYVER=\"$(CHRYVER)\" -DLICENSE=\"$(LICENSE)\"
CXXFLAGS = $(COMMON_FLAGS) -fno-exceptions -fno-rtti -DKERNEL_CODENAME=\"$(KERNEL_CODENAME)\" -DCHRYVER=\"$(CHRYVER)\" -DLICENSE=\"$(LICENSE)\"

ASFLAGS = -f elf32
LDFLAGS = -m elf_i386 -T linker.ld

BUILD = build
ISO = iso
KERNEL = kernel.bin

OBJS = \
	$(BUILD)/user/libpetal.o \
	build/arch/i386/multiboot2_header.o \
	$(BUILD)/boot.o \
	$(BUILD)/gdt_flush.o \
	$(BUILD)/gdt.o \
	$(BUILD)/kernel.o \
	$(BUILD)/idt.o \
	$(BUILD)/irq.o \
	$(BUILD)/interrupts.o \
	$(BUILD)/keyboard.o \
	$(BUILD)/pic.o \
	$(BUILD)/shell.o \
	$(BUILD)/isr.o \
	$(BUILD)/terminal.o \
	$(BUILD)/stdio_impl.o \
	$(BUILD)/cmd_clear.o \
	$(BUILD)/cmd_reboot.o \
	$(BUILD)/cmd_shutdown.o \
	$(BUILD)/cmd_registry.o \
	$(BUILD)/bootlogo.o \
	$(BUILD)/fs.o \
	$(BUILD)/cmd_ls.o \
	$(BUILD)/cmd_cat.o \
	$(BUILD)/cmd_touch.o \
	$(BUILD)/pit.o \
	$(BUILD)/load.o \
	$(BUILD)/serial.o \
	$(BUILD)/rtc.o \
	$(BUILD)/clock.o \
	$(BUILD)/cmd_date.o \
	$(BUILD)/speaker.o \
	$(BUILD)/cmd_beep.o \
	$(BUILD)/audio.o \
	$(BUILD)/shortcuts.o \
	$(BUILD)/string.o \
	$(BUILD)/cmd_play.o \
	$(BUILD)/mlb.o \
	$(BUILD)/mouse.o \
	$(BUILD)/keyboard_buffer.o \
	$(BUILD)/keymap.o \
	$(BUILD)/keymap_us.o \
	$(BUILD)/keymap_ro.o \
	$(BUILD)/vga.o \
	$(BUILD)/timer.o \
	$(BUILD)/cmd_uptime.o \
	$(BUILD)/cmd_ticks.o \
	$(BUILD)/cmd_help.o \
	$(BUILD)/event_queue.o \
	$(BUILD)/ata.o \
	$(BUILD)/disk.o \
	$(BUILD)/fat_fs.o \
	$(BUILD)/cmd_fat.o \
	$(BUILD)/vfs.o \
	$(BUILD)/vfs_extra.o \
	$(BUILD)/ramfs.o \
	$(BUILD)/ramfs_add.o \
	$(BUILD)/cmd_vfs.o \
	$(BUILD)/pmm.o \
	$(BUILD)/cmd_pmm.o \
	$(BUILD)/paging.o \
	$(BUILD)/user.o \
	$(BUILD)/cmd_login.o \
	$(BUILD)/mem/kmalloc.o \
	$(BUILD)/mem/kmalloc_init.o \
	$(BUILD)/mem/buddy.o \
	$(BUILD)/mem/slab.o \
	$(BUILD)/user_blob.o \
	$(BUILD)/panic.o \
	$(BUILD)/cmd_crash.o \
	$(BUILD)/panic_sys.o \
	$(BUILD)/cmd_mem.o \
	$(BUILD)/address_space.o \
	$(BUILD)/user_mem.o \
	$(BUILD)/copy_user.o \
	$(BUILD)/vmm.o \
	$(BUILD)/mm/paging.o \
	$(BUILD)/load_cr3.o \
	$(BUILD)/task/task.o \
	$(BUILD)/task/sched.o \
	$(BUILD)/arch/switch.o \
	$(BUILD)/sched/pcb.o \
	$(BUILD)/sched/scheduler.o \
	$(BUILD)/ram.o \
	$(BUILD)/tpm.o \
	$(BUILD)/videomemory.o \
	$(BUILD)/virtualbox.o \
	$(BUILD)/cmd_credits.o \
	$(BUILD)/cmd_echo.o \
	$(BUILD)/cmd_fortune.o \
	$(BUILD)/cmd_buildinfo.o \
	$(BUILD)/cmd_sysfetch.o \
	$(BUILD)/cmd_chrysver.o \
	$(BUILD)/arch/tss.o \
	$(BUILD)/arch/tss_flush.o \
	$(BUILD)/arch/syscall.o \
	$(BUILD)/arch/syscall_asm.o \
	$(BUILD)/arch/isr_default.o \
	$(BUILD)/arch/syscall_dispatch.o \
	$(BUILD)/arch/setjmp.o \
	$(BUILD)/elf.o \
	$(BUILD)/process.o \
	$(BUILD)/exec.o \
	$(BUILD)/stubs/exec_fallbacks.o \
	$(BUILD)/cmds/elf.o \
	$(BUILD)/cmds/elf_debug.o \
	$(BUILD)/cmds/elf_crash.o \
	$(BUILD)/cmds/write.o \
	$(BUILD)/cmds/cs.o \
	$(BUILD)/cmds/vt_cmd.o \
	$(BUILD)/cmds/color.o \
	$(BUILD)/cmds/rm.o \
	$(BUILD)/cmds/mkdir.o \
	$(BUILD)/cmds/cd.o \
	$(BUILD)/cmds/win.o \
	$(BUILD)/cmds/net.o \
	$(BUILD)/cmds/get.o \
	$(BUILD)/cmds/curl.o \
	$(BUILD)/cmds/pkg.o \
	$(BUILD)/cmds/pathutil.o \
	$(BUILD)/cmds/bash_support.o \
	$(BUILD)/cmds/pwd.o \
	$(BUILD)/cmds/cp.o \
	$(BUILD)/cmds/mv.o \
	$(BUILD)/cmds/head.o \
	$(BUILD)/cmds/tail.o \
	$(BUILD)/cmds/wc.o \
	$(BUILD)/cmds/hexdump.o \
	$(BUILD)/cmds/grep.o \
	$(BUILD)/cmds/tee.o \
	$(BUILD)/cmds/sha256.o \
	$(BUILD)/cmds/sleep.o \
	$(BUILD)/cmds/command_exec.o \
	$(BUILD)/cmds/which.o \
	$(BUILD)/cmds/gcc.o \
	$(BUILD)/cmds/size.o \
	$(BUILD)/chryspkg/chryspkg.o \
	$(BUILD)/hardware/pci.o \
	$(BUILD)/hardware/acpi.o \
	$(BUILD)/arch/interrupts.o \
	$(BUILD)/hardware/msr.o \
	$(BUILD)/hardware/apic.o \
	$(BUILD)/hardware/lapic.o \
	$(BUILD)/hardware/ioapic.o \
	$(BUILD)/hardware/hpet.o \
	$(BUILD)/usb/usb_core.o \
	$(BUILD)/usb/uhci.o \
	$(BUILD)/usb/usb_hid.o \
	$(BUILD)/usb/usb_hub.o \
	$(BUILD)/usb/usb_msc.o \
	$(BUILD)/smp/smp.o \
	$(BUILD)/smp/trampoline_obj.o \
	build/storage/ahci/ahci_pci.o \
    build/storage/ahci/ahci_hba.o \
    build/storage/ahci/ahci_port.o \
    build/storage/ahci/ahci_cmd.o \
    build/storage/ahci/ahci_dma.o \
    build/storage/partition.o \
    build/storage/io_sched.o \
    build/storage/block.o \
    build/input/input.o \
    build/fs/chrysfs/chrysfs.o \
	$(BUILD)/framebuffer.o \
	$(BUILD)/fb_console.o \
	$(BUILD)/gpu.o \
	$(BUILD)/gpu_bochs.o \
	$(BUILD)/vt.o \
	$(BUILD)/colors/cl.o \
	$(BUILD)/surface.o \
	$(BUILD)/compositor.o \
	$(BUILD)/ui/wm/wm.o \
	$(BUILD)/ui/wm/window.o \
	$(BUILD)/ui/wm/wm_layout.o \
	$(BUILD)/ui/wm/wm_hooks.o \
	$(BUILD)/ui/flyui/core.o \
	$(BUILD)/ui/flyui/widget.o \
	$(BUILD)/ui/flyui/event.o \
	$(BUILD)/ui/flyui/bmp.o \
	$(BUILD)/ui/flyui/draw.o \
	$(BUILD)/ui/flyui/surface.o \
	$(BUILD)/ui/flyui/layout.o \
	$(BUILD)/ui/flyui/theme.o \
	$(BUILD)/ui/flyui/widgets/label.o \
	$(BUILD)/ui/flyui/widgets/button.o \
	$(BUILD)/ui/flyui/widgets/panel.o \
	$(BUILD)/ui/flyui/widgets/image.o \
	$(BUILD)/ui/flyui/widgets/icon_button.o \
	$(BUILD)/ui/flyui/widgets/textbox.o \
	$(BUILD)/toolchain/cc.o \
	$(BUILD)/apps/app_manager.o \
	$(BUILD)/apps/calculator_app.o \
	$(BUILD)/apps/calendar_app.o \
	$(BUILD)/apps/clock_app.o \
	$(BUILD)/apps/demo3d_app.o \
	$(BUILD)/apps/file_manager_app.o \
	$(BUILD)/apps/image_viewer_app.o \
	$(BUILD)/apps/minesweeper_app.o \
	$(BUILD)/apps/notepad_app.o \
	$(BUILD)/apps/paint_app.o \
	$(BUILD)/apps/run_dialog_app.o \
	$(BUILD)/apps/sysinfo_app.o \
	$(BUILD)/apps/task_manager_app.o \
	$(BUILD)/apps/tic_tac_toe_app.o \
	$(BUILD)/apps/icons/icons.o \
	$(BUILD)/ethernet/net.o \
	$(BUILD)/ethernet/net_device.o \
	$(BUILD)/ethernet/eth.o \
	$(BUILD)/ethernet/arp.o \
	$(BUILD)/ethernet/ipv4.o \
	$(BUILD)/ethernet/udp.o \
	$(BUILD)/ethernet/tcp.o \
	$(BUILD)/ethernet/tls.o \
	$(BUILD)/ethernet/dns.o \
	$(BUILD)/ethernet/dhcp.o \
	$(BUILD)/ethernet/drivers/e1000.o \
	$(BUILD)/crypto/sha256.o \
	$(BUILD)/crypto/aes.o \
	$(BUILD)/crypto/prng.o \
	$(BUILD)/ethernet/drivers/rtl8139.o



# -------------------------
# TARGETURI PRINCIPALE
# -------------------------

.PHONY: all iso run clean help consolerun dirs icons petal_apps

all: $(ISO)/boot/$(KERNEL) petal_apps

$(BUILD):
	mkdir -p $(BUILD)

dirs: $(BUILD)
	mkdir -p $(BUILD)/elf
	mkdir -p $(BUILD)/proc
	mkdir -p $(BUILD)/stubs
	mkdir -p $(BUILD)/cmds
	mkdir -p $(BUILD)/arch
	mkdir -p $(BUILD)/arch/i386
	mkdir -p $(BUILD)/arch/i386/cpu
	mkdir -p $(BUILD)/arch/i386/apic
	mkdir -p $(BUILD)/smp
	mkdir -p $(BUILD)/hardware
	mkdir -p ${BUILD}/colors
	mkdir -p $(BUILD)/ui/wm
	mkdir -p $(BUILD)/ui/flyui
	mkdir -p $(BUILD)/toolchain
	mkdir -p $(BUILD)/apps
	mkdir -p ${BUILD}/ethernet
	mkdir -p ${BUILD}/ethernet/drivers
	mkdir -p ${BUILD}/crypto
	mkdir -p ${BUILD}/chryspkg
	mkdir -p ${BUILD}/user
	mkdir -p ${BUILD}/storage/ahci
	mkdir -p $(ISO)/system/apps





# -------------------------
# ASSEMBLY
# -------------------------

$(BUILD)/boot.o: kernel/boot.asm | dirs
	$(AS) $(ASFLAGS) $< -o $@

$(BUILD)/gdt_flush.o: kernel/arch/i386/gdt.asm | dirs
	$(AS) $(ASFLAGS) $< -o $@

$(BUILD)/interrupts.o: kernel/interrupts/interrupts.asm | dirs
	$(AS) $(ASFLAGS) $< -o $@

$(BUILD)/arch/setjmp.o: kernel/arch/i386/setjmp.asm | dirs
	$(AS) $(ASFLAGS) $< -o $@
# -------------------------
# C++ OBJECTS
# -------------------------
build/arch/i386/multiboot2_header.o: arch/i386/multiboot2_header.c | dirs
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD)/kernel.o: kernel/kernel.cpp | dirs
	$(CXX) $(CXXFLAGS) -c $< -o $@

$(BUILD)/idt.o: kernel/arch/i386/idt.cpp | dirs
	$(CXX) $(CXXFLAGS) -c $< -o $@

$(BUILD)/irq.o: kernel/interrupts/irq.cpp | dirs
	$(CXX) $(CXXFLAGS) -c $< -o $@

$(BUILD)/keyboard.o: kernel/drivers/keyboard.cpp | dirs
	$(CXX) $(CXXFLAGS) -c $< -o $@

$(BUILD)/terminal.o: kernel/terminal.cpp | dirs
	$(CXX) $(CXXFLAGS) -c $< -o $@

$(BUILD)/pic.o: kernel/drivers/pic.cpp | dirs
	$(CXX) $(CXXFLAGS) -c $< -o $@

$(BUILD)/gdt.o: kernel/arch/i386/gdt.cpp | dirs
	$(CXX) $(CXXFLAGS) -c $< -o $@

$(BUILD)/isr.o: kernel/interrupts/isr.cpp | dirs
	$(CXX) $(CXXFLAGS) -c $< -o $@

$(BUILD)/shell.o: kernel/shell/shell.cpp | dirs
	$(CXX) $(CXXFLAGS) -c $< -o $@

$(BUILD)/stdio_impl.o: kernel/stdio_impl.c | dirs
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD)/cmd_clear.o: kernel/cmds/clear.cpp | dirs
	$(CXX) $(CXXFLAGS) -c $< -o $@

$(BUILD)/cmd_reboot.o: kernel/cmds/reboot.cpp | dirs
	$(CXX) $(CXXFLAGS) -c $< -o $@

$(BUILD)/cmd_shutdown.o: kernel/cmds/shutdown.cpp | dirs
	$(CXX) $(CXXFLAGS) -c $< -o $@

$(BUILD)/cmd_registry.o: kernel/cmds/registry.cpp | dirs
	$(CXX) $(CXXFLAGS) -c $< -o $@

$(BUILD)/fs.o: kernel/fs/fs.cpp | dirs
	$(CXX) $(CXXFLAGS) -c $< -o $@

$(BUILD)/cmd_ls.o: kernel/cmds/ls.cpp | dirs
	$(CXX) $(CXXFLAGS) -c $< -o $@

$(BUILD)/cmd_cat.o: kernel/cmds/cat.cpp | dirs
	$(CXX) $(CXXFLAGS) -c $< -o $@

$(BUILD)/cmd_touch.o: kernel/cmds/touch.cpp | dirs
	$(CXX) $(CXXFLAGS) -c $< -o $@

$(BUILD)/bootlogo.o: kernel/bootlogo/bootlogo.cpp | dirs
	$(CXX) $(CXXFLAGS) -c $< -o $@

$(BUILD)/pit.o: kernel/drivers/pit.cpp | dirs
	$(CXX) $(CXXFLAGS) -c $< -o $@

$(BUILD)/load.o: kernel/debug/load.cpp | dirs
	$(CXX) $(CXXFLAGS) -c $< -o $@

$(BUILD)/serial.o: kernel/drivers/serial.cpp | dirs
	$(CXX) $(CXXFLAGS) -c $< -o $@

$(BUILD)/rtc.o: kernel/drivers/rtc.cpp | dirs
	$(CXX) $(CXXFLAGS) -c $< -o $@

$(BUILD)/clock.o: kernel/time/clock.cpp | dirs
	$(CXX) $(CXXFLAGS) -c $< -o $@

$(BUILD)/cmd_date.o: kernel/cmds/date.cpp | dirs
	$(CXX) $(CXXFLAGS) -c $< -o $@

$(BUILD)/speaker.o: kernel/drivers/speaker.cpp | dirs
	$(CXX) $(CXXFLAGS) -c $< -o $@

$(BUILD)/cmd_beep.o: kernel/cmds/beep.cpp | dirs
	$(CXX) $(CXXFLAGS) -c $< -o $@

$(BUILD)/audio.o: kernel/drivers/audio/audio.cpp | dirs
	$(CXX) $(CXXFLAGS) -c $< -o $@

$(BUILD)/shortcuts.o: kernel/shortcuts/shortcuts.cpp | dirs
	$(CXX) $(CXXFLAGS) -c $< -o $@

$(BUILD)/string.o: kernel/string.cpp | dirs
	$(CXX) $(CXXFLAGS) -c $< -o $@

$(BUILD)/cmd_play.o: kernel/cmds/play.cpp | dirs
	$(CXX) $(CXXFLAGS) -c $< -o $@

$(BUILD)/mlb.o: kernel/mlb/mlb.cpp | dirs
	$(CXX) $(CXXFLAGS) -c $< -o $@

$(BUILD)/mouse.o: kernel/drivers/mouse.c | dirs
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD)/keyboard_buffer.o: kernel/input/keyboard_buffer.cpp | dirs
	$(CXX) $(CXXFLAGS) -c $< -o $@

$(BUILD)/keymap.o: kernel/keyboard/keymap.c | dirs
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD)/keymap_us.o: kernel/keyboard/keymap_us.c | dirs
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD)/keymap_ro.o: kernel/keyboard/keymap_ro.c | dirs
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD)/vga.o: kernel/video/vga.c | dirs
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD)/timer.o: kernel/time/timer.c | dirs
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD)/cmd_uptime.o: kernel/cmds/uptime.cpp | dirs
	$(CXX) $(CXXFLAGS) -c $< -o $@

$(BUILD)/cmd_ticks.o: kernel/cmds/ticks.cpp | dirs
	$(CXX) $(CXXFLAGS) -c $< -o $@

$(BUILD)/cmd_help.o: kernel/cmds/help.cpp | dirs
	$(CXX) $(CXXFLAGS) -c $< -o $@

$(BUILD)/event_queue.o: kernel/events/event_queue.c | dirs
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD)/disk.o: kernel/cmds/disk.cpp | dirs
	$(CXX) $(CXXFLAGS) -c $< -o $@

$(BUILD)/cmd_fat.o: kernel/cmds/fat.cpp | dirs
	$(CXX) $(CXXFLAGS) -c $< -o $@

$(BUILD)/vfs.o: kernel/fs/vfs/vfs.c | dirs
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD)/vfs_extra.o: kernel/fs/vfs/vfs_extra.c | dirs
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD)/ramfs.o: kernel/fs/ramfs/ramfs.c | dirs
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD)/ramfs_add.o: kernel/fs/ramfs/ramfs_add.c | dirs
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD)/cmd_vfs.o: kernel/cmds/vfs.cpp | dirs
	$(CXX) $(CXXFLAGS) -c $< -o $@

$(BUILD)/user.o: kernel/user/user.cpp | dirs
	$(CXX) $(CXXFLAGS) -c $< -o $@

$(BUILD)/cmd_login.o: kernel/cmds/login.cpp | dirs
	$(CXX) $(CXXFLAGS) -c $< -o $@

$(BUILD)/panic.o: kernel/panic.cpp kernel/panic.h | dirs
	$(CXX) $(CXXFLAGS) -c $< -o $@

$(BUILD)/cmd_crash.o: kernel/cmds/crash.cpp kernel/cmds/crash.h | dirs
	$(CXX) $(CXXFLAGS) -c $< -o $@

# -------------------------
# C OBJECTS (folosim gcc)
# -------------------------

$(BUILD)/ata.o: kernel/storage/ata.c | dirs
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD)/fat_fs.o: kernel/fs/fat/fat.c | dirs
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD)/pmm.o: kernel/memory/pmm.c | dirs
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD)/cmd_pmm.o: kernel/cmds/pmm.cpp | dirs
	$(CXX) $(CXXFLAGS) -c $< -o $@

$(BUILD)/paging.o: kernel/paging.c kernel/paging.h | dirs
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD)/mem/%.o: kernel/mem/%.c | dirs
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -Ikernel/include -ffreestanding -c $< -o $@

$(BUILD)/panic_sys.o: kernel/panic_sys.c | dirs
	$(CC) $(CFLAGS) -c $< -o $@

build/user_prog.bin: kernel/user/user_prog.S | dirs
	$(AS) -f bin $< -o $@

$(BUILD)/user_blob.o: kernel/user/user_blob.c build/user_prog.bin | dirs
	$(CXX) $(CXXFLAGS) -c $< -o $@

$(BUILD)/cmd_mem.o: kernel/cmds/mem.cpp | dirs
	$(CXX) $(CXXFLAGS) -c $< -o $@

$(BUILD)/address_space.o: kernel/mm/address_space.c | dirs
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD)/user_mem.o: kernel/mm/user_mem.c | dirs
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD)/copy_user.o: kernel/mm/copy_user.c | dirs
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD)/vmm.o: kernel/mm/vmm.c | dirs
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD)/mm/paging.o: kernel/mm/paging.c | dirs
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD)/load_cr3.o: arch/x86/load_cr3.S | dirs
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD)/task/%.o: kernel/task/%.c | dirs
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -Ikernel/include -c $< -o $@

# compile assembly (AT&T) for i386
$(BUILD)/arch/%.o: arch/i386/%.S | dirs
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -m32 -ffreestanding -c $< -o $@

$(BUILD)/sched/pcb.o: kernel/sched/pcb.c kernel/sched/pcb.h | dirs
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -m32 -c $< -o $@

$(BUILD)/sched/scheduler.o: kernel/sched/scheduler.c kernel/sched/scheduler.h kernel/sched/pcb.h | dirs
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -m32 -c $< -o $@

$(BUILD)/ram.o: kernel/detect/ram.c | dirs
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD)/tpm.o: kernel/detect/tpm.c | dirs
	$(CC) $(CFLAGS) -Ikernel/include -c $< -o $@

$(BUILD)/videomemory.o: kernel/detect/videomemory.c | dirs
	$(CC) $(CFLAGS) -Ikernel/include -c $< -o $@

$(BUILD)/virtualbox.o: kernel/detect/virtualbox.cpp | dirs
	$(CXX) $(CXXFLAGS) -Ikernel/include -c $< -o $@

$(BUILD)/cmd_credits.o: kernel/cmds/credits.cpp | dirs
	$(CXX) $(CXXFLAGS) -c $< -o $@

$(BUILD)/cmd_echo.o: kernel/cmds/echo.cpp | dirs
	$(CXX) $(CXXFLAGS) -c $< -o $@

$(BUILD)/cmd_fortune.o: kernel/cmds/fortune.cpp | dirs
	$(CXX) $(CXXFLAGS) -c $< -o $@

$(BUILD)/cmd_buildinfo.o: kernel/cmds/buildinfo.cpp | dirs
	$(CXX) $(CXXFLAGS) -c $< -o $@

$(BUILD)/cmd_sysfetch.o: kernel/cmds/sysfetch.cpp | dirs
	$(CXX) $(CXXFLAGS) -c $< -o $@

$(BUILD)/cmd_chrysver.o: kernel/cmds/chrysver.cpp | dirs
	$(CXX) $(CXXFLAGS) -c $< -o $@

$(BUILD)/arch/tss.o: kernel/arch/i386/tss.c | dirs
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD)/arch/tss_flush.o: kernel/arch/i386/tss_flush.S | dirs
	$(AS) $(ASFLAGS) $< -o $@

$(BUILD)/arch/syscall.o: kernel/arch/i386/syscall.c | dirs
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD)/arch/syscall_asm.o: kernel/arch/i386/syscall.S | dirs
	$(AS) $(ASFLAGS) $< -o $@

$(BUILD)/arch/isr_default.o: kernel/arch/i386/isr_default.S | dirs
	$(AS) $(ASFLAGS) $< -o $@

$(BUILD)/arch/syscall_dispatch.o: kernel/arch/i386/syscall_dispatch.cpp | dirs
	$(CXX) $(CXXFLAGS) -c $< -o $@

$(BUILD)/elf.o: kernel/elf/elf.c | dirs
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD)/process.o: kernel/proc/process.c | dirs
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD)/exec.o: kernel/proc/exec.cpp | dirs
	$(CXX) $(CXXFLAGS) -c $< -o $@

$(BUILD)/stubs/exec_fallbacks.o: kernel/stubs/exec_fallbacks.c | dirs
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD)/cmds/elf.o: kernel/cmds/elf.cpp | dirs
	$(CXX) $(CXXFLAGS) -c $< -o $@

$(BUILD)/cmds/elf_debug.o: kernel/cmds/elf_debug.cpp | dirs
	$(CXX) $(CXXFLAGS) -c $< -o $@

$(BUILD)/cmds/elf_crash.o: kernel/cmds/elf_crash.cpp | dirs
	$(CXX) $(CXXFLAGS) -c $< -o $@

$(BUILD)/cmds/cs.o: kernel/cmds/cs.cpp | dirs
	$(CXX) $(CXXFLAGS) -c $< -o $@

$(BUILD)/cmds/write.o: kernel/cmds/write.cpp | dirs
	$(CXX) $(CXXFLAGS) -c $< -o $@

$(BUILD)/cmds/vt_cmd.o: kernel/cmds/vt_cmd.cpp | dirs
	$(CXX) $(CXXFLAGS) -c $< -o $@

$(BUILD)/cmds/color.o: kernel/cmds/color.cpp | dirs
	$(CXX) $(CXXFLAGS) -c $< -o $@

$(BUILD)/cmds/rm.o: kernel/cmds/rm.cpp | dirs
	$(CXX) $(CXXFLAGS) -c $< -o $@

$(BUILD)/cmds/mkdir.o: kernel/cmds/mkdir.cpp | dirs
	$(CXX) $(CXXFLAGS) -c $< -o $@

$(BUILD)/cmds/cd.o: kernel/cmds/cd.cpp | dirs
	$(CXX) $(CXXFLAGS) -c $< -o $@

$(BUILD)/cmds/win.o: kernel/cmds/win.cpp | dirs
	$(CXX) $(CXXFLAGS) -c $< -o $@

$(BUILD)/cmds/net.o: kernel/cmds/net.cpp | dirs
	$(CXX) $(CXXFLAGS) -c $< -o $@

$(BUILD)/cmds/get.o: kernel/cmds/get.cpp | dirs
	$(CXX) $(CXXFLAGS) -c $< -o $@

$(BUILD)/cmds/curl.o: kernel/cmds/curl.cpp | dirs
	$(CXX) $(CXXFLAGS) -c $< -o $@

$(BUILD)/cmds/pkg.o: kernel/cmds/pkg.cpp | dirs
	$(CXX) $(CXXFLAGS) -c $< -o $@

$(BUILD)/cmds/pathutil.o: kernel/cmds/pathutil.cpp | dirs
	$(CXX) $(CXXFLAGS) -c $< -o $@

$(BUILD)/cmds/bash_support.o: kernel/cmds/bash_support.cpp | dirs
	$(CXX) $(CXXFLAGS) -c $< -o $@

$(BUILD)/cmds/pwd.o: kernel/cmds/pwd.cpp | dirs
	$(CXX) $(CXXFLAGS) -c $< -o $@

$(BUILD)/cmds/cp.o: kernel/cmds/cp.cpp | dirs
	$(CXX) $(CXXFLAGS) -c $< -o $@

$(BUILD)/cmds/mv.o: kernel/cmds/mv.cpp | dirs
	$(CXX) $(CXXFLAGS) -c $< -o $@

$(BUILD)/cmds/head.o: kernel/cmds/head.cpp | dirs
	$(CXX) $(CXXFLAGS) -c $< -o $@

$(BUILD)/cmds/tail.o: kernel/cmds/tail.cpp | dirs
	$(CXX) $(CXXFLAGS) -c $< -o $@

$(BUILD)/cmds/wc.o: kernel/cmds/wc.cpp | dirs
	$(CXX) $(CXXFLAGS) -c $< -o $@

$(BUILD)/cmds/hexdump.o: kernel/cmds/hexdump.cpp | dirs
	$(CXX) $(CXXFLAGS) -c $< -o $@

$(BUILD)/cmds/grep.o: kernel/cmds/grep.cpp | dirs
	$(CXX) $(CXXFLAGS) -c $< -o $@

$(BUILD)/cmds/tee.o: kernel/cmds/tee.cpp | dirs
	$(CXX) $(CXXFLAGS) -c $< -o $@

$(BUILD)/cmds/sha256.o: kernel/cmds/sha256.cpp | dirs
	$(CXX) $(CXXFLAGS) -c $< -o $@

$(BUILD)/cmds/sleep.o: kernel/cmds/sleep.cpp | dirs
	$(CXX) $(CXXFLAGS) -c $< -o $@

$(BUILD)/cmds/command_exec.o: kernel/cmds/command_exec.cpp | dirs
	$(CXX) $(CXXFLAGS) -c $< -o $@

$(BUILD)/cmds/which.o: kernel/cmds/which.cpp | dirs
	$(CXX) $(CXXFLAGS) -c $< -o $@

$(BUILD)/cmds/size.o: kernel/cmds/size.cpp | dirs
	$(CXX) $(CXXFLAGS) -c $< -o $@

$(BUILD)/cmds/gcc.o: kernel/cmds/gcc.cpp | dirs
	$(CXX) $(CXXFLAGS) -c $< -o $@

$(BUILD)/chryspkg/chryspkg.o: kernel/chryspkg/chryspkg.c | dirs
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD)/hardware/pci.o: kernel/hardware/pci.cpp
	@mkdir -p $(BUILD)/hardware
	$(CXX) $(CXXFLAGS) -c $< -o $@

$(BUILD)/hardware/acpi.o: kernel/hardware/acpi.cpp
	$(CXX) $(CXXFLAGS) -c $< -o $@

$(BUILD)/arch/interrupts.o: kernel/arch/i386/interrupts.c | dirs
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD)/hardware/msr.o: kernel/hardware/msr.c | dirs
	@mkdir -p $(BUILD)/hardware
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD)/hardware/apic.o: kernel/hardware/apic.c | dirs
	@mkdir -p $(BUILD)/hardware
	$(CXX) $(CXXFLAGS) -c $< -o $@

$(BUILD)/hardware/lapic.o: kernel/hardware/lapic.c | dirs
	@mkdir -p $(BUILD)/hardware
	$(CXX) $(CXXFLAGS) -c $< -o $@

$(BUILD)/hardware/ioapic.o: kernel/hardware/ioapic.c | dirs
	@mkdir -p $(BUILD)/hardware
	$(CXX) $(CXXFLAGS) -c $< -o $@

$(BUILD)/hardware/hpet.o: kernel/hardware/hpet.cpp | dirs
	@mkdir -p $(BUILD)/hardware
	$(CXX) $(CXXFLAGS) -c $< -o $@

$(BUILD)/usb/usb_core.o: kernel/usb/usb_core.c | dirs
	@mkdir -p $(BUILD)/usb
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD)/usb/uhci.o: kernel/usb/uhci.c | dirs
	@mkdir -p $(BUILD)/usb
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD)/usb/usb_hid.o: kernel/usb/usb_hid.c | dirs
	@mkdir -p $(BUILD)/usb
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD)/usb/usb_hub.o: kernel/usb/usb_hub.c | dirs
	@mkdir -p $(BUILD)/usb
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD)/usb/usb_msc.o: kernel/usb/usb_msc.c | dirs
	@mkdir -p $(BUILD)/usb
	$(CC) $(CFLAGS) -c $< -o $@

# -------------------------
# SMP & Trampoline
# -------------------------

$(BUILD)/smp/smp.o: kernel/smp/smp.c kernel/smp/smp.h | dirs
	$(CC) $(CFLAGS) -c $< -o $@

# Rules for the trampoline
# This creates a flat binary of the trampoline code
$(BUILD)/smp/trampoline.bin: kernel/smp/trampoline.S | dirs
	$(AS) -f bin $< -o $@

# Convert binary trampoline to object file to link with kernel
$(BUILD)/smp/trampoline_obj.o: $(BUILD)/smp/trampoline.bin
	objcopy -I binary -O elf32-i386 -B i386 $< $@

# Note: You will need to ensure trampoline.bin is available to the kernel, e.g., via GRUB module or embedding.

build/storage/ahci/ahci_pci.o: kernel/storage/ahci/ahci_pci.c kernel/storage/ahci/ahci.h
	@mkdir -p build/storage/ahci
	$(CC) $(CFLAGS) -c kernel/storage/ahci/ahci_pci.c -o $@

build/storage/ahci/ahci_hba.o: kernel/storage/ahci/ahci_hba.c kernel/storage/ahci/ahci.h
	@mkdir -p build/storage/ahci
	$(CC) $(CFLAGS) -c kernel/storage/ahci/ahci_hba.c -o $@

build/storage/ahci/ahci_port.o: kernel/storage/ahci/ahci_port.c kernel/storage/ahci/ahci.h
	@mkdir -p build/storage/ahci
	$(CC) $(CFLAGS) -c kernel/storage/ahci/ahci_port.c -o $@

build/storage/ahci/ahci_cmd.o: kernel/storage/ahci/ahci_cmd.c kernel/storage/ahci/ahci.h
	@mkdir -p build/storage/ahci
	$(CC) $(CFLAGS) -c kernel/storage/ahci/ahci_cmd.c -o $@

build/storage/ahci/ahci_dma.o: kernel/storage/ahci/ahci_dma.c kernel/storage/ahci/ahci.h
	@mkdir -p build/storage/ahci
	$(CC) $(CFLAGS) -c kernel/storage/ahci/ahci_dma.c -o $@

build/storage/partition.o: kernel/storage/partition.c
	@mkdir -p build/storage
	$(CC) $(CFLAGS) -c kernel/storage/partition.c -o $@

build/storage/io_sched.o: kernel/storage/io_sched.c
	@mkdir -p build/storage
	$(CC) $(CFLAGS) -c kernel/storage/io_sched.c -o $@

build/storage/block.o: kernel/storage/block.c
	@mkdir -p build/storage
	$(CC) $(CFLAGS) -c kernel/storage/block.c -o $@

build/input/input.o: kernel/input/input.c
	@mkdir -p build/input
	$(CC) $(CFLAGS) -c kernel/input/input.c -o $@

build/fs/chrysfs/chrysfs.o: kernel/fs/chrysfs/chrysfs.c
	@mkdir -p build/fs/chrysfs
	$(CC) $(CFLAGS) -c kernel/fs/chrysfs/chrysfs.c -o $@

$(BUILD)/framebuffer.o: kernel/video/framebuffer.c | dirs
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD)/fb_console.o: kernel/video/fb_console.c | dirs
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD)/gpu.o: kernel/video/gpu.c | dirs
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD)/gpu_bochs.o: kernel/video/gpu_bochs.c | dirs
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD)/vt.o: kernel/vt/vt.c | dirs
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD)/colors/cl.o: kernel/colors/cl.c | dirs
	$(CC) $(CFLAGS) -c $< -o $@

# -------------------------
# USERLAND (LibPetal & Apps)
# -------------------------

$(BUILD)/user/libpetal.o: user/libpetal/src/libpetal.c | dirs
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c $< -o $@

PETAL_APPS_SRC = $(wildcard apps/*.cpp)
PETAL_APPS = $(patsubst apps/%.cpp, $(ISO)/system/apps/%.petal, $(PETAL_APPS_SRC))

PETAL_CXXFLAGS = $(CXXFLAGS) -fno-pic -Iuser/libpetal/include
PETAL_LDFLAGS = -m elf_i386
PETAL_HEADERS = user/libpetal/include/petal.h

$(BUILD)/user/start.o: user/libpetal/src/start.asm | dirs
	$(AS) -f elf32 $< -o $@

$(ISO)/system/apps/%.petal: apps/%.cpp $(BUILD)/user/libpetal.o $(BUILD)/user/start.o $(PETAL_HEADERS) | dirs
	@mkdir -p $(dir $@)
	$(CXX) $(PETAL_CXXFLAGS) -c $< -o $(BUILD)/apps/$*.o
	$(LD) $(PETAL_LDFLAGS) $(BUILD)/user/start.o $(BUILD)/apps/$*.o $(BUILD)/user/libpetal.o -o $@

petal_apps: $(PETAL_APPS)

$(BUILD)/surface.o: kernel/video/surface.c | dirs
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD)/compositor.o: kernel/video/compositor.c | dirs
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD)/ui/wm/wm.o: kernel/ui/wm/wm.c | dirs
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD)/ui/wm/window.o: kernel/ui/wm/window.c | dirs
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD)/ui/wm/wm_layout.o: kernel/ui/wm/wm_layout.c | dirs
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD)/ui/wm/wm_hooks.o: kernel/ui/wm/wm_hooks.c | dirs
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD)/ui/flyui/core.o: kernel/ui/flyui/core.c | dirs
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD)/ui/flyui/widget.o: kernel/ui/flyui/widget.c | dirs
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD)/ui/flyui/event.o: kernel/ui/flyui/event.c | dirs
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD)/ui/flyui/bmp.o: kernel/ui/flyui/bmp.c | dirs
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD)/ui/flyui/draw.o: kernel/ui/flyui/draw.c | dirs
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD)/ui/flyui/surface.o: kernel/ui/flyui/surface.c | dirs
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD)/ui/flyui/layout.o: kernel/ui/flyui/layout.c | dirs
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD)/ui/flyui/theme.o: kernel/ui/flyui/theme.c | dirs
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD)/ui/flyui/widgets/label.o: kernel/ui/flyui/widgets/label.c | dirs
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD)/ui/flyui/widgets/button.o: kernel/ui/flyui/widgets/button.c | dirs
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD)/ui/flyui/widgets/panel.o: kernel/ui/flyui/widgets/panel.c | dirs
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD)/ui/flyui/widgets/image.o: kernel/ui/flyui/widgets/image.c | dirs
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD)/ui/flyui/widgets/icon_button.o: kernel/ui/flyui/widgets/icon_button.c | dirs
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD)/ui/flyui/widgets/textbox.o: kernel/ui/flyui/widgets/textbox.c | dirs
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD)/toolchain/%.o: kernel/toolchain/%.cpp | dirs
	@mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS) -c $< -o $@

$(BUILD)/apps/%.o: kernel/apps/%.cpp | dirs
	@mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS) -c $< -o $@

$(BUILD)/apps/icons/icons.o: kernel/apps/icons/icons.c | dirs
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD)/ethernet/%.o: kernel/ethernet/%.c | dirs
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD)/crypto/%.o: kernel/crypto/%.c | dirs
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c $< -o $@

# -------------------------
# LINK
# -------------------------

LIBGCC := $(shell $(CC) $(CFLAGS) -print-libgcc-file-name)

$(ISO)/boot/$(KERNEL): $(OBJS) | dirs
	@mkdir -p $(dir $@)
	$(LD) $(LDFLAGS) -o $@ $(OBJS) $(LIBGCC)

# -------------------------
# ISO & RUN
# -------------------------

icons:
	@echo "[ICONS] Generating icons BMPs..."
	cd ../tools/icons && python3 mkicons.py
	@mkdir -p $(ISO)/icons
	@mkdir -p $(ISO)/system
	cp ../tools/icons/out/*.bmp $(ISO)/icons/
	@if [ -f ../tools/icons/out/bg.bmp ]; then \
		cp ../tools/icons/out/bg.bmp $(ISO)/system/bg.bmp; \
		echo "[ICONS] Wallpaper copied to $(ISO)/system/bg.bmp"; \
	else echo "WARN: bg.bmp not found in tools/icons/out/"; fi

iso: all iso/boot/grub/grub.cfg icons
	@mkdir -p $(ISO)/boot/grub
	@mkdir -p $(ISO)/system/pkg/installed
	@cp kernel/chryspkg/catalog.json $(ISO)/system/pkg/catalog.json
	@echo "[GRUB] Installing theme..."
	@mkdir -p $(ISO)/boot/grub/themes/chrysalis
	@cp ../grub-theme/* $(ISO)/boot/grub/themes/chrysalis/ 2>/dev/null || true
	@echo "[GRUB] Creating ISO with graphical modules..."
	GRUB_FONT_PATH=/usr/share/grub grub-mkrescue -o chrysalis.iso --modules="gfxterm gfxmenu png tga jpeg font all_video" $(ISO) 2>&1 | grep -v "xorriso:"

run: iso
	qemu-system-x86_64 -cdrom chrysalis.iso -m 512

consolerun: iso
	qemu-system-x86_64 -cdrom chrysalis.iso -nographic

clean:
	rm -rf $(BUILD) *.iso $(ISO)/boot/$(KERNEL) $(ISO)/icons ../tools/icons/out

# -------------------------
# ASSETS
# -------------------------
assets:
	mcopy -i hdd.img@@1M -o kernel/ui/flyui/assets/wallpaper.bmp ::/bg.bmp

# -------------------------
# HELP
# -------------------------

help:
	@echo ""
	@echo "Chrysalis OS - Makefile commands"
	@echo "--------------------------------"
	@echo "make           -> compilează kernelul + app-urile .petal"
	@echo "make iso       -> creează imaginea bootabilă ISO"
	@echo "make run       -> rulează OS-ul în QEMU (cu grafică)"
	@echo "make consolerun-> rulează OS-ul în QEMU (doar consolă)"
	@echo "make clean     -> șterge fișierele generate"
	@echo "make assets    -> copiază wallpaper.bmp în hdd.img (necesită mtools)"
	@echo "make help      -> afișează acest mesaj"
	@echo ""
