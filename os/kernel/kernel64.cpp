/*
 * Chrysalis OS (x86_64 prototype)
 * Mirror of kernel.cpp flow with TODO markers for unported subsystems.
 */

#include <stdint.h>
#include <stddef.h>

#include "arch/i386/io.h"
#include "drivers/serial.h"
#include "smp/multiboot.h"
#include "string.h"

extern "C" void paging64_init(void);
extern "C" void idt64_init(void);
extern "C" void syscall64_init(void);

struct mb64_module {
  const char *name;
  uint32_t start;
  uint32_t end;
};

static mb64_module g_mb_modules[32];
static int g_mb_module_count = 0;
static uint64_t g_total_ram_mb = 0;

static volatile uint16_t *g_vga = (uint16_t *)0xB8000;
static int g_vga_row = 0;
static int g_vga_col = 0;
static uint8_t g_vga_attr = 0x0F;

static bool g_force_pic = false;
static bool g_boot_gui = false;

static void vga_clear(void) {
  for (int y = 0; y < 25; y++) {
    for (int x = 0; x < 80; x++) {
      g_vga[y * 80 + x] = (uint16_t)(' ' | (g_vga_attr << 8));
    }
  }
  g_vga_row = 0;
  g_vga_col = 0;
}

static void vga_scroll(void) {
  if (g_vga_row < 25)
    return;
  for (int y = 1; y < 25; y++) {
    for (int x = 0; x < 80; x++) {
      g_vga[(y - 1) * 80 + x] = g_vga[y * 80 + x];
    }
  }
  for (int x = 0; x < 80; x++) {
    g_vga[24 * 80 + x] = (uint16_t)(' ' | (g_vga_attr << 8));
  }
  g_vga_row = 24;
}

static void vga_putc(char c) {
  if (c == '\n') {
    g_vga_col = 0;
    g_vga_row++;
    vga_scroll();
    return;
  }
  if (c == '\r') {
    g_vga_col = 0;
    return;
  }
  if (c == '\b') {
    if (g_vga_col > 0) {
      g_vga_col--;
      g_vga[g_vga_row * 80 + g_vga_col] =
          (uint16_t)(' ' | (g_vga_attr << 8));
    }
    return;
  }
  g_vga[g_vga_row * 80 + g_vga_col] = (uint16_t)(c | (g_vga_attr << 8));
  g_vga_col++;
  if (g_vga_col >= 80) {
    g_vga_col = 0;
    g_vga_row++;
    vga_scroll();
  }
}

static void vga_puts(const char *s) {
  if (!s)
    return;
  while (*s)
    vga_putc(*s++);
}

static void vga_put_u32(uint32_t v) {
  char buf[16];
  int i = 0;
  if (v == 0) {
    buf[i++] = '0';
  } else {
    while (v && i < 15) {
      buf[i++] = (char)('0' + (v % 10));
      v /= 10;
    }
  }
  while (i--)
    vga_putc(buf[i]);
}

static void vga_put_hex32(uint32_t v) {
  const char *hex = "0123456789abcdef";
  vga_puts("0x");
  for (int i = 28; i >= 0; i -= 4) {
    vga_putc(hex[(v >> i) & 0xF]);
  }
}

static void k64_todo(const char *msg) {
  vga_puts("[TODO] ");
  vga_puts(msg);
  vga_putc('\n');
  serial_write_string("[TODO] ");
  serial_write_string(msg);
  serial_write_string("\r\n");
}

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
  }
  if (cmdline_has_token(cmdline, "gui=1") ||
      cmdline_has_token(cmdline, "boot=gui")) {
    g_boot_gui = true;
  }
}

static void shell64_prompt(void) {
  vga_puts("\nK64> ");
  serial_write_string("\r\nK64> ");
}

static int shell64_readline(char *buf, int max) {
  int len = 0;
  while (len < max - 1) {
    char c = serial_read();
    if (c == '\r' || c == '\n') {
      vga_putc('\n');
      buf[len] = 0;
      return len;
    }
    if (c == '\b' || c == 127) {
      if (len > 0) {
        len--;
        vga_putc('\b');
        serial_write('\b');
      }
      continue;
    }
    if (c >= 32 && c <= 126) {
      buf[len++] = c;
      vga_putc(c);
      serial_write(c);
    }
  }
  buf[len] = 0;
  return len;
}

static void shell64_cmd_help(void) {
  vga_puts("Commands: help, echo, uname, mods, ram, reboot, halt\n");
  serial_write_string("Commands: help, echo, uname, mods, ram, reboot, halt\r\n");
}

static void shell64_cmd_uname(void) {
  vga_puts("ChrysalisOS x86_64 prototype\n");
  serial_write_string("ChrysalisOS x86_64 prototype\r\n");
}

static void shell64_cmd_mods(void) {
  vga_puts("Modules:\n");
  serial_write_string("Modules:\r\n");
  for (int i = 0; i < g_mb_module_count; i++) {
    vga_puts("  ");
    vga_puts(g_mb_modules[i].name ? g_mb_modules[i].name : "(unnamed)");
    vga_puts(" @");
    vga_put_hex32(g_mb_modules[i].start);
    vga_puts(" size=");
    vga_put_u32(g_mb_modules[i].end - g_mb_modules[i].start);
    vga_putc('\n');
  }
}

static void shell64_cmd_ram(void) {
  vga_puts("RAM: ");
  vga_put_u32((uint32_t)g_total_ram_mb);
  vga_puts(" MB\n");
}

static void shell64_exec(char *line) {
  if (!line || !*line)
    return;
  char *argv[16];
  int argc = 0;
  char *p = line;
  while (*p && argc < 16) {
    while (*p == ' ')
      p++;
    if (!*p)
      break;
    argv[argc++] = p;
    while (*p && *p != ' ')
      p++;
    if (*p)
      *p++ = 0;
  }
  if (argc == 0)
    return;

  if (strcmp(argv[0], "help") == 0) {
    shell64_cmd_help();
  } else if (strcmp(argv[0], "echo") == 0) {
    for (int i = 1; i < argc; i++) {
      if (i > 1)
        vga_putc(' ');
      vga_puts(argv[i]);
    }
    vga_putc('\n');
  } else if (strcmp(argv[0], "uname") == 0) {
    shell64_cmd_uname();
  } else if (strcmp(argv[0], "mods") == 0) {
    shell64_cmd_mods();
  } else if (strcmp(argv[0], "ram") == 0) {
    shell64_cmd_ram();
  } else if (strcmp(argv[0], "reboot") == 0) {
    serial_write_string("Rebooting...\r\n");
    outb(0x64, 0xFE);
    for (;;) {
      asm volatile("hlt");
    }
  } else if (strcmp(argv[0], "halt") == 0) {
    serial_write_string("Halting...\r\n");
    for (;;) {
      asm volatile("hlt");
    }
  } else {
    vga_puts("Unknown command. Type 'help'.\n");
    serial_write_string("Unknown command. Type 'help'.\r\n");
  }
}

extern "C" void kernel_main64(unsigned long long magic,
                              unsigned long long info) {
  (void)magic;

  vga_clear();
  vga_puts("ChrysalisOS 64-bit kernel (prototype)\n");

  paging64_init();
  idt64_init();
  syscall64_init();
  serial_init();
  serial_write_string("[K64] serial online\r\n");

  const char *boot_cmdline = NULL;

  if (info) {
    struct multiboot2_tag *tag =
        (struct multiboot2_tag *)((uintptr_t)info + 8);
    while (tag->type != MULTIBOOT2_TAG_TYPE_END) {
      if (tag->type == MULTIBOOT2_TAG_TYPE_CMDLINE) {
        struct multiboot2_tag_string *cs =
            (struct multiboot2_tag_string *)tag;
        boot_cmdline = cs->string;
        parse_cmdline(boot_cmdline);
      }
      if (tag->type == MULTIBOOT2_TAG_TYPE_BASIC_MEMINFO) {
        struct multiboot2_tag_basic_meminfo *mem =
            (struct multiboot2_tag_basic_meminfo *)tag;
        if (g_total_ram_mb == 0)
          g_total_ram_mb = (mem->mem_lower + mem->mem_upper) / 1024;
      } else if (tag->type == MULTIBOOT2_TAG_TYPE_MMAP) {
        struct multiboot2_tag_mmap *mmap =
            (struct multiboot2_tag_mmap *)tag;
        uint64_t total_bytes = 0;
        for (struct multiboot2_mmap_entry *entry = mmap->entries;
             (uint8_t *)entry < (uint8_t *)mmap + mmap->common.size;
             entry = (struct multiboot2_mmap_entry *)((uint8_t *)entry +
                                                      mmap->entry_size)) {
          if (entry->type == MULTIBOOT2_MEMORY_AVAILABLE) {
            total_bytes += entry->len;
          }
        }
        g_total_ram_mb = total_bytes / (1024 * 1024);
      } else if (tag->type == MULTIBOOT2_TAG_TYPE_MODULE) {
        struct multiboot2_tag_module *mod =
            (struct multiboot2_tag_module *)tag;
        if (g_mb_module_count < 32) {
          g_mb_modules[g_mb_module_count].start = mod->mod_start;
          g_mb_modules[g_mb_module_count].end = mod->mod_end;
          g_mb_modules[g_mb_module_count].name = mod->string;
          g_mb_module_count++;
        }
      }
      tag = (struct multiboot2_tag *)((uint8_t *)tag + ((tag->size + 7) & ~7));
    }
  }

  vga_puts("RAM detected: ");
  vga_put_u32((uint32_t)g_total_ram_mb);
  vga_puts(" MB\n");

  /* TODO: virtualbox_check_or_panic() */
  /* TODO: pmm_init (64-bit physical memory manager) */
  /* TODO: gdt_init + tss_init for x86_64 */
  /* TODO: idt_init + isr_install + irq_install */
  /* TODO: pic_remap / ioapic routing */
  /* TODO: timer_init + scheduler */
  /* TODO: fs_init + ramfs_root + vfs_mount */
  /* TODO: user_init + toolchain_init + services_init */
  /* TODO: input drivers (keyboard/mouse) */
  /* TODO: window manager + GUI */
  k64_todo("Port core subsystems from kernel.cpp to x86_64.");

  vga_puts("\n[K64] Ready. Use serial console for input.\n");
  serial_write_string("[K64] Ready. Use serial console for input.\r\n");

  char line[128];
  while (1) {
    shell64_prompt();
    if (shell64_readline(line, sizeof(line)) >= 0) {
      shell64_exec(line);
    }
  }
}

extern "C" void isr64_handler(void) {
  vga_puts("\n[K64] Unhandled exception.\n");
  serial_write_string("\r\n[K64] Unhandled exception.\r\n");
  for (;;) {
    asm volatile("hlt");
  }
}
