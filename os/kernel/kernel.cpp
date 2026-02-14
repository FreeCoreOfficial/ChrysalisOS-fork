/*
 * Chrysalis OS
 * Copyright (c) 2026 mihai209
 *
 * Licensed under the MIT License.
 * You may use, modify, and redistribute this software,
 * provided that the original author is credited and
 * this notice is preserved.
 */

// kernel/kernel.cpp
// Updated: defensive task integration + panic fallback (tasks disabled by
// default)
//
// Notes:
//  - TASKS_ENABLED = 0 by default to avoid bootloops caused by incomplete
//  context-switch.
//  - To re-enable: set TASKS_ENABLED to 1, ensure you have a correct
//  arch/i386/switch.S
//    that saves/restores full CPU context (pushad/popad) and returns to a valid
//    stack.
//  - Triple faults cannot be caught — avoid them by not switching into invalid
//  stacks.
//  - For harder safety, implement a double-fault handler in your IDT that calls
//  panic()
//    so many kernel faults will show a panic screen instead of resulting in a
//    triple fault.

#include "include/types.h"
#include <stdarg.h>

#include "arch/i386/gdt.h"
#include "arch/i386/idt.h"
#include "arch/i386/io.h"
#include "arch/i386/syscall.h"
#include "arch/i386/tss.h"
#include "bootlogo/bootlogo.h"
#include "cmds/disk.h"
#include "cmds/fat.h"
#include "cmds/win.h"
#include "colors/cl.h"
#include "detect/virtualbox.h"
#include "drivers/audio/audio.h"
#include "drivers/keyboard.h"
#include "drivers/mouse.h"
#include "drivers/pic.h"
#include "drivers/rtc.h"
#include "drivers/serial.h"
#include "ethernet/net.h"
#include "events/event_queue.h"
#include "fs/chrysfs/chrysfs.h"
#include "fs/fs.h"
#include "fs/ramfs/ramfs.h"
#include "fs/vfs/mount.h"
#include "hardware/acpi.h"
#include "hardware/apic.h"
#include "hardware/hpet.h"
#include "hardware/pci.h"
#include "include/types.h"
#include "input/input.h"
#include "input/keyboard_buffer.h"
#include "interrupts/irq.h"
#include "interrupts/isr.h"
#include "mem/kmalloc.h"
#include "memory/pmm.h"
#include "mm/vmm.h"
#include "paging.h"
#include "panic.h"
#include "panic_sys.h"
#include "shell/shell.h"
#include "smp/multiboot.h"
#include "smp/smp.h"
#include "storage/ahci/ahci.h"
#include "storage/ata.h"
#include "storage/block.h"
#include "storage/io_sched.h"
#include "string.h"
#include "terminal.h"
#include "time/clock.h"
#include "time/timer.h"
#include "toolchain/cc.h"
#include "usb/usb_core.h"
#include "user/user.h"
#include "video/fb_console.h"
#include "video/framebuffer.h"
#include "video/gpu.h"
#include "video/gpu_bochs.h"
#include "vt/vt.h"

static bool g_force_pic = false;
static bool g_boot_gui = false;

static bool cmdline_has_token(const char *cmdline, const char *tok) {
  if (!cmdline || !tok)
    return false;
  size_t tlen = strlen(tok);
  const char *p = cmdline;
  while (*p) {
    while (*p == ' ')
      p++;
    if (!*p)
      break;
    const char *start = p;
    while (*p && *p != ' ')
      p++;
    size_t len = (size_t)(p - start);
    if (len == tlen && strncmp(start, tok, tlen) == 0) {
      return true;
    }
  }
  return false;
}

static void parse_cmdline(const char *cmdline) {
  if (!cmdline || !*cmdline)
    return;
  if (cmdline_has_token(cmdline, "apic=off") ||
      cmdline_has_token(cmdline, "noapic")) {
    g_force_pic = true;
    apic_set_forced_off(true);
  }
  if (cmdline_has_token(cmdline, "gui=1") ||
      cmdline_has_token(cmdline, "boot=gui")) {
    g_boot_gui = true;
  }
}
#include "include/task.h"

// ===== AHCI / Driver Support Glue =====
extern "C" {

char *itoa_dec(char *out, int32_t v);
char *utoa_hex(char *out, uint32_t v);

/* Minimal serial printf implementation for drivers */
void serial(const char *fmt, ...) {
  char buf[1024];
  char *p = buf;
  va_list args;
  va_start(args, fmt);

  const char *f = fmt;
  while (*f) {
    if (*f != '%') {
      *p++ = *f++;
      continue;
    }
    f++; // skip %

    // Simple modifiers skip (like .40 or 08)
    while ((*f >= '0' && *f <= '9') || *f == '.')
      f++;

    if (*f == 'd' || *f == 'u') {
      int val = va_arg(args, int);
      char tmp[32];
      itoa_dec(tmp, val);
      char *t = tmp;
      while (*t)
        *p++ = *t++;
    } else if (*f == 'x' || *f == 'p') {
      uint32_t val = va_arg(args, uint32_t);
      char tmp[32];
      utoa_hex(tmp, val);
      char *t = tmp;
      while (*t)
        *p++ = *t++;
    } else if (*f == 's') {
      const char *s = va_arg(args, const char *);
      if (!s)
        s = "(null)";
      while (*s)
        *p++ = *s++;
    } else if (*f == 'l') {
      f++; // skip first l
      if (*f == 'l')
        f++; // skip second l
      if (*f == 'u') {
        // Hack for %llu: print high/low hex or just 32-bit cast for now
        // Since we don't have full 64-bit formatting helpers here easily
        uint64_t val = va_arg(args, uint64_t);
        char tmp[32];
        utoa_hex(tmp,
                 (uint32_t)val); // Truncate to 32-bit for simplicity in glue
        char *t = tmp;
        while (*t)
          *p++ = *t++;
      }
    } else {
      *p++ = '?';
    }
    f++;
  }
  *p = 0;
  va_end(args);

  serial_write_string(buf);
}

/* Uptime helper for AHCI timeouts */
uint32_t get_uptime_ms(void) {
  if (hpet_is_active()) {
    return (uint32_t)hpet_time_ms();
  }
  // Fallback if HPET not active (should not happen if hpet_init called)
  return 0;
}

} // extern "C"

// ===== end fallback =====

// ===== other forward declarations / externs =====
/* If shell.h doesn't declare shell_poll_input(), provide a local fallback */
#ifdef __cplusplus
extern "C" {
#endif
void shell_handle_char(char c); /* Asigurăm vizibilitatea funcției */
void shell_poll_input(void);
#ifdef __cplusplus
}
#endif

extern "C" {
void kmalloc_init(void);
}

/* Global multiboot module info to preserve until kmalloc is ready */
typedef struct {
  void *data;
  uint32_t size;
  const char *name;
} mb_file_t;

static mb_file_t g_multiboot_modules[32];
static char g_multiboot_names[32][32];
static int g_multiboot_module_count = 0;

static void mb_copy_name(const char *src, char *dst, size_t dst_sz) {
  if (!dst || dst_sz == 0)
    return;
  dst[0] = 0;
  if (!src)
    return;
  while (*src && (unsigned char)*src <= ' ')
    src++;
  const char *end = src;
  while (*end)
    end++;
  while (end > src && (unsigned char)end[-1] <= ' ')
    end--;
  const char *token = src;
  for (const char *p = src; p < end; ++p) {
    if (*p == ' ')
      token = p + 1;
  }
  const char *base = token;
  for (const char *p = token; p < end; ++p) {
    if (*p == '/' || *p == '\\')
      base = p + 1;
  }
  size_t n = (size_t)(end - base);
  if (n >= dst_sz)
    n = dst_sz - 1;
  memcpy(dst, base, n);
  dst[n] = 0;
}

/* Buddy allocator wrapper that hooks the buddy allocator to the heap region
   defined by the linker script (linker.ld) */
extern "C" void buddy_init_from_heap(void);

/* Helper to switch terminal backend */
extern "C" void terminal_set_backend_fb(bool active);

/* IMPORTANT: by default we KEEP TASKING DISABLED to avoid bootloops.
   Set to 1 only after you're sure switch_to is correct and you have tested
   context switching in a controlled environment.
*/
#define TASKS_ENABLED 0

// -----------------------------
// Example tasks (cooperative)
// -----------------------------
// These demonstrate the cooperative API: tasks MUST call task_yield() so the
// scheduler can switch. If a task never yields, it will monopolize the CPU.
//
// NOTE: With TASKS_ENABLED==0 these are present but not started.
void task_a(void) {
  for (;;) {
    terminal_writestring("[taskA] Task A running\n");
    for (volatile int i = 0; i < 1000000; ++i)
      ;
    task_yield(); /* yield to scheduler */
  }
}

void task_b(void) {
  for (;;) {
    terminal_writestring("[taskB] Task B running\n");
    for (volatile int i = 0; i < 1000000; ++i)
      ;
    task_yield();
  }
}

// -----------------------------
// Helper: panic on kernel-detected fatal error
// -----------------------------
// Use this function for early fatal errors so the kernel shows the panic screen
// instead of continuing in a corrupted state. Note: this cannot catch hardware
// triple-faults that already reset the CPU.
static void panic_if_fatal(const char *msg) {
  // try to print something sensible to both terminal and serial
  if (msg) {
    terminal_writestring("[PANIC] ");
    terminal_writestring(msg);
    terminal_writestring("\n");
    serial_write_string("[PANIC] ");
    serial_write_string(msg);
    serial_write_string("\r\n");
  }
  // register a minimal panic state and halt (panic() should not return)
  panic("Kernel detected fatal error; halting.");
}

// -----------------------------
// kernel_main - entry point
// -----------------------------
extern "C" void kernel_main(uint32_t magic, uint32_t addr) {

  // 2) Terminal (text-mode) initialization - early so we can show errors
  terminal_init();

  // Check for VirtualBox and panic if detected
  virtualbox_check_or_panic();

  const char *boot_cmdline = NULL;

  // 3) Early boot safety check: detect RAM and panic cleanly if insufficient

  uint64_t total_ram_mb = 0;
  if (magic == MULTIBOOT2_BOOTLOADER_MAGIC && addr != 0) {
    struct multiboot2_tag *tag = (struct multiboot2_tag *)(uintptr_t)(addr + 8);
    while (tag->type != MULTIBOOT2_TAG_TYPE_END) {
      if (tag->type == MULTIBOOT2_TAG_TYPE_CMDLINE) {
        struct multiboot2_tag_string *cs = (struct multiboot2_tag_string *)tag;
        boot_cmdline = cs->string;
        parse_cmdline(boot_cmdline);
      }
      if (tag->type == MULTIBOOT2_TAG_TYPE_BASIC_MEMINFO) {
        struct multiboot2_tag_basic_meminfo *mem =
            (struct multiboot2_tag_basic_meminfo *)tag;
        // Fallback if MMAP not present (mem_upper is in KB)
        if (total_ram_mb == 0)
          total_ram_mb = (mem->mem_lower + mem->mem_upper) / 1024;
      } else if (tag->type == MULTIBOOT2_TAG_TYPE_MMAP) {
        struct multiboot2_tag_mmap *mmap = (struct multiboot2_tag_mmap *)tag;
        uint64_t total_bytes = 0;
        for (struct multiboot2_mmap_entry *entry = mmap->entries;
             (uint8_t *)entry < (uint8_t *)mmap + mmap->common.size;
             entry = (struct multiboot2_mmap_entry *)((uint8_t *)entry +
                                                      mmap->entry_size)) {
          if (entry->type == MULTIBOOT2_MEMORY_AVAILABLE) {
            total_bytes += entry->len;
          }
        }
        total_ram_mb = total_bytes / (1024 * 1024);
      } else if (tag->type == MULTIBOOT2_TAG_TYPE_MODULE) {
        struct multiboot2_tag_module *mod = (struct multiboot2_tag_module *)tag;
        uint32_t size = mod->mod_end - mod->mod_start;
        if (g_multiboot_module_count < (int)(sizeof(g_multiboot_modules) /
                                             sizeof(g_multiboot_modules[0]))) {
          char *namebuf = g_multiboot_names[g_multiboot_module_count];
          mb_copy_name(mod->string, namebuf, sizeof(g_multiboot_names[0]));
          g_multiboot_modules[g_multiboot_module_count].data =
              (void *)(uintptr_t)mod->mod_start;
          g_multiboot_modules[g_multiboot_module_count].size = size;
          g_multiboot_modules[g_multiboot_module_count].name = namebuf;
          serial("[KERNEL] Multiboot Module detected: %s at 0x%x (%u bytes)\n",
                 namebuf, mod->mod_start, size);
          g_multiboot_module_count++;
        } else {
          serial("[KERNEL] Multiboot Module skipped (too many)\n");
        }
      }
      tag = (struct multiboot2_tag *)((uint8_t *)tag + ((tag->size + 7) & ~7));
    }
  } else if (magic == MULTIBOOT_MAGIC && addr != 0) {
    multiboot_info_t *mb = (multiboot_info_t *)(uintptr_t)addr;
    if (mb->flags & (1 << 2)) {
      boot_cmdline = (const char *)(uintptr_t)mb->cmdline;
      parse_cmdline(boot_cmdline);
    }
    if (mb->flags & 1) {
      total_ram_mb = (mb->mem_lower + mb->mem_upper) / 1024;
    }
  }

  serial("[RAM] Detected: %u MB\n", (uint32_t)total_ram_mb);

  if (total_ram_mb < 256) {
    panic_if_fatal(
        "Insufficient RAM (detected < 256 MB). Check bootloader/QEMU memory.");
  }

  // 9) Physical Memory Manager (PMM) - MOVED UP
  // Must be initialized BEFORE paging or heap
  pmm_init(magic, (void *)(uintptr_t)addr);

  // 4) CPU/interrupt basic setup: GDT -> IDT -> PIC. Order matters.
  gdt_init();

  syscall_init();

  uint32_t kernel_stack;
  asm volatile("mov %%esp, %0" : "=r"(kernel_stack));

  tss_init(kernel_stack);

  terminal_writestring("[gdt] ring3 segments loaded\n");

  idt_init();
  pic_remap();

  // Instalează handlerele pentru excepții (0-31) - CRITIC pentru debug (#PF,
  // #GP)
  isr_install();

  // Instalează stub-urile ASM și setează handler-ul default (silent) pentru
  // toate IRQ-urile
  irq_install();

  // 11) Timer abstraction (PIT) - install handlers (overwrites IRQ0)
  timer_init(100);

  /* Force unmask IRQ0 (Timer) in PIC Master to ensure timer_get_ticks() works
   */
  outb(0x21, inb(0x21) & 0xFE);

  // 5) Optional boot logo
  bootlogo_show();

  // 6) Filesystem core: initialize and mount a minimal root
  fs_init();
  if (!ramfs_root()) {
    // ramfs_root failure is fatal for our simple early boot; panic instead of
    // continuing
    panic_if_fatal("ramfs_root() returned NULL");
  }
  vfs_mount("/", ramfs_root());

  vnode_t *v = vfs_resolve("/");
  if (v)
    terminal_writestring("[vfs] root mounted OK\n");
  else
    terminal_writestring("[vfs] mount FAILED\n");

  // 7) User subsystem (if present)
  user_init();

  // Init Toolchain
  toolchain_init();

  // 8) Shell: text UI
  // shell_init(); // Moved later (after WM init) to run inside a window

  // 19) Heap + buddy allocator + kmalloc (MOVED UP - CRITICAL for drivers)
  extern uint8_t __heap_start;
  extern uint8_t __heap_end;

  // defensive check: heap range must make sense
  if (&__heap_end <= &__heap_start) {
    panic_if_fatal("Heap region invalid (heap_end <= heap_start)");
  }

  heap_init(&__heap_start, (size_t)(&__heap_end - &__heap_start));
  buddy_init_from_heap();
  kmalloc_init();

  /* Register multiboot modules directly in RAMFS (don't copy - just point to
   * them) */
  if (g_multiboot_module_count > 0) {
    for (int i = 0; i < g_multiboot_module_count; i++) {
      if (!g_multiboot_modules[i].data || g_multiboot_modules[i].size == 0)
        continue;
      if (!g_multiboot_modules[i].name || g_multiboot_modules[i].name[0] == 0)
        continue;
      ramfs_create_file(g_multiboot_modules[i].name,
                        g_multiboot_modules[i].data,
                        g_multiboot_modules[i].size);
      serial("[KERNEL] Multiboot Module registered in RAMFS: %s (%u bytes)\n",
             g_multiboot_modules[i].name, g_multiboot_modules[i].size);
    }
  }

  // Sanity check PMM (if you have functions for total frames, check them here)

  // 10) Input drivers: keyboard buffer + raw keyboard driver
  kbd_buffer_init();
  keyboard_init();

  /* === NEW === */
  /* Use task_init() which is a safe weak symbol fallback (no-op if no task
   * impl). */
  task_init();

  // 12) Other drivers and serial for debug/log
  audio_init();
  mouse_init();

  serial_init();
  serial_write_string("=== Chrysalis OS serial online ===\r\n");

  // 13) Event queue for event-driven design
  event_queue_init();

  terminal_writestring("\nSystem ready.\n> ");

  // 16) RTC/time/ATA
  rtc_print();
  time_init();
  time_set_timezone(2);
  datetime t;
  time_get_local(&t);

  /* ata_init() moved above to conditional block */

  // 17) PMM quick test
  uint32_t frame1 = pmm_alloc_frame();
  uint32_t frame2 = pmm_alloc_frame();
  terminal_printf("PMM: %x %x\n", frame1, frame2);
  if (frame1 == 0 || frame2 == 0) {
    // allocation failure — panic instead of continuing in degraded state
    panic_if_fatal("pmm_alloc_frame returned 0 (allocation failed)");
  }
  pmm_free_frame(frame1);
  pmm_free_frame(frame2);

  // 18) Paging: choose page area size per your roadmap
  paging_init(PAGING_120_MB);

  // FIX: Disable Write Protect (CR0.WP) to allow kernel to write to RO pages
  uint32_t cr0;
  asm volatile("mov %%cr0, %0" : "=r"(cr0));
  cr0 &= ~(1 << 16);
  asm volatile("mov %0, %%cr0" ::"r"(cr0));

  paging_map_kernel_higher_half();

  /* Sync VMM with active paging directory (Bridge legacy paging with new VMM)
   */
  {
    uint32_t cr3_phys;
    asm volatile("mov %%cr3, %0" : "=r"(cr3_phys));
    kernel_page_directory =
        (uint32_t *)((uintptr_t)(cr3_phys & 0xFFFFF000) + 0xC0000000);
  }

  /* FIX: Map VGA memory to avoid crash when printing "Paging OK" */
  vmm_identity_map(0xB8000, 0x1000);

  terminal_printf("Paging OK\n");

  // Initialize GPU Core Subsystem (Moved after paging init to ensure mappings
  // persist)
  gpu_init();

  // Try to initialize Bochs/QEMU GPU (Native driver preferred over VESA)
  gpu_bochs_init();

  /* Initialize VESA Framebuffer (Multiboot 2 preferred) */
  // FIX: Only init VESA if no other GPU (like Bochs) was detected
  if (!gpu_get_primary()) {
    if (magic == MULTIBOOT2_BOOTLOADER_MAGIC && addr != 0) {
      serial("[KERNEL] Multiboot 2 detected.\n");

      // Iterate tags
      struct multiboot2_tag *tag =
          (struct multiboot2_tag *)(uintptr_t)(addr + 8);

      while (tag->type != MULTIBOOT2_TAG_TYPE_END) {
        if (tag->type == MULTIBOOT2_TAG_TYPE_FRAMEBUFFER) {
          struct multiboot2_tag_framebuffer *fb =
              (struct multiboot2_tag_framebuffer *)tag;
          fb_init(fb->common_addr, fb->common_width, fb->common_height,
                  fb->common_pitch, fb->common_bpp, fb->common_type);
          break;
        }

        // Move to next tag (8-byte aligned)
        uint32_t next_off = (tag->size + 7) & ~7;
        tag = (struct multiboot2_tag *)((uint8_t *)tag + next_off);
      }
    } else if (magic == MULTIBOOT_MAGIC && addr != 0) {
      // Legacy Multiboot 1 fallback
      multiboot_info_t *mb = (multiboot_info_t *)(uintptr_t)addr;
      if (mb->flags & (1 << 12)) {
        fb_init(mb->framebuffer_addr, mb->framebuffer_width,
                mb->framebuffer_height, mb->framebuffer_pitch,
                mb->framebuffer_bpp, mb->framebuffer_type);
      }
    }
  } else {
    serial("[KERNEL] Primary GPU already present (Bochs/QEMU), skipping VESA "
           "init.\n");

    /* FIX: Register the existing GPU with the framebuffer abstraction so the
     * compositor sees it */
    gpu_device_t *gpu = gpu_get_primary();
    if (gpu) {
      fb_init(gpu->phys_addr, gpu->width, gpu->height, gpu->pitch, gpu->bpp,
              1 /* RGB */);
    }
  }

  /* Visual test: Draw a blue rectangle to confirm video works */
  if (magic == MULTIBOOT2_BOOTLOADER_MAGIC || magic == MULTIBOOT_MAGIC) {
    /* Visual test: Draw a blue rectangle to confirm video works */
    // Use generic GPU ops instead of hardcoded VESA calls
    gpu_device_t *gpu = gpu_get_primary();
    if (gpu && gpu->ops && gpu->ops->fillrect) {
      gpu->ops->fillrect(gpu, 100, 100, 200, 150, 0x0000FF);
      gpu->ops->fillrect(gpu, 350, 100, 200, 150, 0x00FF0000);
    }

    /* Initialize Colors */
    cl_init();

    /* Initialize Framebuffer Console and redirect terminal output */
    fb_cons_init();
    terminal_set_backend_fb(true);
  }

  /* Initialize Virtual Terminals (must be after fb_cons_init) */
  vt_init();

  /* GUI Subsystems (Compositor, WM) are now initialized by 'launch' command */

  /* Initialize Shell (Text Mode) */
  shell_init();

  input_init();
  block_init();
  chrysfs_init();

  /* BIOS + ACPI legacy areas */
  vmm_identity_map(0x00000000, 0x1000);  // BDA
  vmm_identity_map(0x000E0000, 0x20000); // BIOS area

  /* FIX: Map AHCI MMIO BAR (QEMU specific usually at 0xFEBF0000, mapping 64KB
   * covers it) */
  /* Must be mapped BEFORE ahci_init attempts to access it */
  vmm_identity_map(0xFEBF0000, 0x10000);

  serial("[KERNEL] initializing AHCI\n");
  int ahci_ports = ahci_init();
  io_sched_init(); /* Init Async IO Scheduler */

  /* Robust Disk Initialization Logic */
  bool disk_found =
      (ahci_ports > 0); /* Assume if ports found, devices might be there */

  if (disk_found) {
    serial("[KERNEL] AHCI initialized (%d ports).\n", ahci_ports);
  }

  if (ahci_ports == 0) {
    serial("[KERNEL] AHCI not active or ahci0 missing. Initializing Legacy "
           "ATA...\n");
    ata_init();
  }

  /* Auto-mount: Scan partitions and try to mount FAT32 */
  if (ahci_ports > 0) {
    serial("[KERNEL] Probing partitions...\n");
    disk_probe_partitions();
    fat_automount();
  }

  /* Initialize Users (load from disk now that FS is ready) */
  terminal_writestring("[kernel] initializing users...\n");
  user_init();

  terminal_writestring("Sleeping 1 second...\n");
  sleep(1000);
  terminal_writestring("Done!\n");

  for (int i = 0; i < 5; i++) {
    terminal_writestring("tick\n");
    sleep(500);
  }

  // ACPI & APIC initialization MUST happen after paging is active
  // because they rely on vmm_map_page / kernel_page_directory.
  acpi_init();
  terminal_writestring("[kernel] acpi_init called\n");

  /* Initialize HPET (High Precision Event Timer) */
  hpet_init();

  if (g_force_pic) {
    terminal_writestring("[kernel] apic=off: staying on PIC\n");
  } else {
    apic_init();
  }

  // SMP Initialization (only if APIC is active)
  if (!g_force_pic && apic_is_active()) {
    if (smp_prepare_aps()) {
      smp_detect_cpus();
      smp_start_aps();
    }
  } else {
    serial("[SMP] Skipped (APIC disabled)\n");
  }

  // 14) Now enable interrupts: only do this after driver IRQ handlers are
  // installed. Enabling earlier risks races where an ISR runs before supporting
  // state is ready.
  asm volatile("sti");
  serial("[KERNEL] Interrupts enabled (STI). System alive.\n");

  /* If IRQs are misrouted (common when IOAPIC/ISO overrides go wrong),
     the timer won't tick. Detect and fallback to PIC. */
  uint64_t t0 = timer_ticks();
  for (volatile uint32_t spin = 0; spin < 20000000; spin++) {
    if (timer_ticks() != t0)
      break;
  }
  if (timer_ticks() == t0) {
    serial("[KERNEL] IRQs not ticking, falling back to PIC...\n");
    apic_disable();
    keyboard_init();
    mouse_init();
  }

  // 20) Heap test (defensive frees)
  void *heap_a = kmalloc(64);
  void *heap_b = kmalloc_aligned(100, 64);
  if (!heap_a || !heap_b) {
    // kmalloc failure early indicates severe problem: panic
    panic_if_fatal("kmalloc/kmalloc_aligned failed during early heap test");
  }
  kfree(heap_a);
  kfree(heap_b);

  // 21) Register system info for panic screen (use safe defaults)
  uint32_t total_kb = 0;
  uint32_t free_kb = 0;
  uint32_t uptime_s = 0;

  if (magic == MULTIBOOT_MAGIC && addr != 0) {
    struct mb_basic {
      uint32_t flags;
      uint32_t mem_lower;
      uint32_t mem_upper;
    };
    struct mb_basic *mb = (struct mb_basic *)(uintptr_t)addr;
    if (mb && (mb->flags & 0x1)) {
      total_kb = mb->mem_lower + mb->mem_upper;
    }
  }

  const char *cpu_detected = panic_sys_cpu_str();
  panic_sys_register_memory_kb(total_kb, free_kb);
  panic_sys_register_storage_kb(0, 0); // populate if you have storage info
  panic_sys_register_cpu_str(cpu_detected);
  panic_sys_register_uptime_seconds(uptime_s);

  // -----------------------------
  // TASKS: Setup cooperative scheduling (DEFENSIVE)
  // -----------------------------
#if TASKS_ENABLED
  terminal_writestring(
      "[sched] tasks created; scheduler disabled by default\n");
  terminal_writestring("[sched] call scheduler_start() manually when ready\n");

  /* Create tasks */
  task_create_name("taskA", task_a);
  task_create_name("taskB", task_b);

  terminal_writestring("[sched] scheduler initialized and tasks created "
                       "(scheduler NOT started)\n");
  terminal_writestring(
      "[sched] To start scheduler manually call: scheduler_start();\n");
#else
  terminal_writestring("[sched] TASKS_ENABLED=0 (scheduler disabled)\n");
#endif

  terminal_writestring("[kernel] initializing PCI\n");
  pci_init();

  /* Initialize USB Subsystem (UHCI) */
  usb_core_init();
  terminal_writestring("[kernel] USB init done.\n");

  /* Initialize Ethernet Subsystem */
  net_init();

  // 22) Main loop: shell polling + halt (no unsafe yields)
  // If you want the shell to be preempted by tasks, move shell into its own
  // task and enable TASKS_ENABLED after implementing a safe switch_to.
  serial("[KERNEL] Entering main loop...\n");
  if (g_boot_gui) {
    serial("[KERNEL] Auto-booting into GUI mode...\n");
    cmd_launch(0, NULL);
  }

  /* Reprint shell prompt as screen might have been cleared or scrolled */
  shell_prompt();

  while (1) {
    usb_poll();                // Poll USB HID devices
    io_sched_poll();           // Process Async I/O requests
    net_poll();                // Poll Network Stack
    ps2_controller_watchdog(); // Scan for PS/2 freezes

    /* NEW: Unified Input Loop */
    input_event_t ev;
    while (input_pop(&ev)) {
      if (ev.type == INPUT_KEYBOARD && ev.pressed) {
        // Convert keycode to char if needed, or pass raw
        // For now assuming keycode is ASCII for demo
        serial("[KERNEL] Input: %c (0x%x)\n", (char)ev.keycode, ev.keycode);
        shell_handle_char((char)ev.keycode);
      } else if (ev.type == INPUT_MOUSE_CLICK && ev.pressed) {
        /* Handle Scroll in Text Mode */
        if (ev.keycode == 4)
          fb_cons_scroll(-3); /* Scroll Up */
        else if (ev.keycode == 5)
          fb_cons_scroll(3); /* Scroll Down */
      }
    }

    yield(); // cooperative switch
  }
}

// =========================
// Notes for modders & distro builders
// =========================
//
// Chrysalis OS is designed to be hacked, extended, and redistributed.
// You are encouraged to build your own distro on top of it (Linux-style),
// with your own UI, drivers, scheduler, or userland.
//
// LICENSE & OWNERSHIP:
//
// - Chrysalis OS is released under the MIT License.
// - You have full freedom to:
//     * modify
//     * fork
//     * redistribute
//     * rebrand
//     * ship commercially or non-commercially
// - The ONLY requirement is to preserve the MIT license notice and copyright
//   attribution to the original owner.
//
// You own your fork. Do whatever you want — responsibly.
//
// -------------------------
// Scheduler & context switching (advanced)
// -------------------------
//
// To implement REAL preemptive multitasking:
//
//   * Implement a robust arch/i386/switch.S that saves and restores full CPU
//   state:
//       - General registers (pushad)
//       - EFLAGS
//       - Segment registers if you use them (cs, ss via iret frame)
//   * Avoid simplistic stack swapping like:
//       movl %esp, (prev)
//       movl (next), %esp
//       ret
//     These techniques break easily in non-trivial kernels.
//
//   * In the PIT (or APIC) IRQ handler:
//       - Save CPU context
//       - Call schedule()
//       - Restore context and return via iret
//
//   * User-mode tasks require a safe iret trampoline
//     with a properly constructed privilege transition frame.
//
// -------------------------
// Fault handling & stability
// -------------------------
//
// To avoid silent resets (triple-faults):
//
//   * Install a valid double-fault handler in the IDT.
//   * The double-fault handler MUST:
//       - Use a known-good stack (TSS recommended)
//       - Print diagnostic info
//       - Halt or drop into a safe recovery console
//
//   * If the double-fault handler itself is broken,
//     the CPU will triple-fault and reset (not catchable).
//
// -------------------------
// Diagnostics & development tips
// -------------------------
//
//   * Keep serial() logging enabled during early boot.
//     It is the most reliable debug channel.
//
//   * When enabling TASKS_ENABLED or SMP:
//       - Always test inside a VM
//       - Use snapshots before major changes
//
//   * Create a small "switch_test" routine:
//       - No interrupts
//       - Tiny stacks
//       - Deterministic switching
//     Then expand gradually to IRQ-driven scheduling.
//
// -------------------------
// Final note
// -------------------------
//
// Chrysalis OS aims to be a foundation, not a cage.
// Build your own distro. Replace everything if you want.
// Just keep the MIT notice — the rest is yours.
//
// End of file
