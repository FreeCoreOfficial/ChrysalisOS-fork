#include <stddef.h>
#include <stdint.h>

#include "../os/kernel/arch/i386/io.h" /* for outb */
#include "../os/kernel/cmds/cd.h"
#include "../os/kernel/cmds/disk.h" /* For disk_init, disk_read_sector etc */
#include "../os/kernel/cmds/fat.h"
#include "../os/kernel/cmds/ls.h"
#include "../os/kernel/cmds/reboot.h"
#include "../os/kernel/cmds/shutdown.h"
#include "../os/kernel/drivers/serial.h"
#include "../os/kernel/fs/fat/fat.h"
#include "../os/kernel/fs/ramfs/ramfs.h"
#include "../os/kernel/include/types.h"
#include "../os/kernel/mem/kmalloc.h"
#include "../os/kernel/smp/multiboot.h"
#include "../os/kernel/storage/ata.h"
#include "../os/kernel/string.h"

#include "../os/kernel/crypto/sha256.h"

/* Local BPB struct for installer debug dump */
struct fat_bpb {
  uint8_t jmp[3];
  char oem[8];
  uint16_t bytes_per_sector;
  uint8_t sectors_per_cluster;
  uint16_t reserved_sectors;
  uint8_t fats_count;
  uint16_t root_entries_count;
  uint16_t total_sectors_16;
  uint8_t media_descriptor;
  uint16_t sectors_per_fat_16;
  uint16_t sectors_per_track;
  uint16_t heads_count;
  uint32_t hidden_sectors;
  uint32_t total_sectors_32;
  uint32_t sectors_per_fat_32;
  uint16_t ext_flags;
  uint16_t fs_version;
  uint32_t root_cluster;
  uint16_t fs_info;
  uint16_t backup_boot_sector;
  uint8_t reserved[12];
  uint8_t drive_number;
  uint8_t reserved1;
  uint8_t boot_signature;
  uint32_t volume_id;
  char volume_label[11];
  char fs_type[8];
} __attribute__((packed));

/* Helper Prototypes */
extern "C" {
void kmalloc_init(void);
void kmalloc_reset(void);
int fat32_format(uint32_t lba, uint32_t sector_count, const char *label);
int fat32_create_directory(const char *path);
int fat32_create_directory_verified(const char *path, int verify);
int fat32_create_file(const char *path, const void *data, uint32_t size);
int fat32_create_file_verified(const char *path, const void *data,
                               uint32_t size, int verify);
void fat32_set_mounted(uint32_t lba, char letter);
int fat32_read_directory(const char *path, fat_file_info_t *out,
                         int max_entries);
int32_t fat32_get_file_size(const char *path);
void terminal_clear(void);
void terminal_putentryat(char c, uint8_t color, int x, int y);
int terminal_get_cursor_x(void);
int terminal_get_cursor_y(void);
void terminal_set_cursor_pos(int x, int y);
extern void fat32_deduplicate_root();
void terminal_putchar(char c);
void terminal_set_color(uint8_t color);
void terminal_printf(const char *fmt, ...);
int fat32_create_file_alloc(const char *path, uint32_t size);
int fat32_write_file_offset(const char *path, const void *data, uint32_t size,
                            uint32_t offset, int verify);
void serial(const char *fmt, ...);
void ata_init(void);
void ata_set_skip_cache_flush(int enabled);
void *kmalloc(size_t size);
void kfree(void *ptr);
void *memset(void *s, int c, size_t n);
void *memcpy(void *dest, const void *src, size_t n);
int strcmp(const char *s1, const char *s2);
size_t strlen(const char *s);
int fat32_init(int port, uint64_t part_lba);
uint32_t disk_get_capacity(void);
int disk_write_sector(uint32_t lba, const uint8_t *buffer);
int disk_read_sector(uint32_t lba, uint8_t *buffer);
void ata_set_allow_mbr_write(int allow);
}

/* Icon filenames (BMP) */
#define ICON_COUNT 16
static const char *g_icon_files[ICON_COUNT] = {
    "start.bmp", "term.bmp",  "files.bmp", "img.bmp",  "note.bmp", "paint.bmp",
    "calc.bmp",  "clock.bmp", "calc.bmp",  "task.bmp", "info.bmp", "3D.bmp",
    "mine.bmp",  "net.bmp",   "x0.bmp",    "run.bmp",
};

static void normalize_module_name_len(const char *cmdline, size_t cmdline_len,
                                      char *out, size_t out_sz) {
  if (!out || out_sz == 0)
    return;
  out[0] = 0;
  if (!cmdline)
    return;

  const char *s = cmdline;
  const char *end = cmdline + cmdline_len;
  while (s < end && (unsigned char)*s <= ' ')
    s++;
  while (end > s && (unsigned char)end[-1] <= ' ')
    end--;
  while (end > s && (unsigned char)end[-1] <= ' ')
    end--;

  const char *token = s;
  for (const char *p = s; p < end; ++p) {
    if (*p == ' ')
      token = p + 1;
  }

  if (*token == '\'' || *token == '"') {
    char q = *token++;
    if (end > token && end[-1] == q)
      end--;
  }

  const char *last = token;
  for (const char *p = token; p < end; ++p) {
    if (*p == '/')
      last = p + 1;
  }

  size_t n = (size_t)(end - last);
  if (n >= out_sz)
    n = out_sz - 1;
  memcpy(out, last, n);
  out[n] = 0;
}

static void write_sectors(uint32_t lba, const void *data, uint32_t bytes) {
  const uint8_t *p = (const uint8_t *)data;
  uint8_t *tmp = (uint8_t *)kmalloc(512);
  if (!tmp)
    return;
  uint32_t sectors = (bytes + 511) / 512;
  for (uint32_t i = 0; i < sectors; i++) {
    uint32_t copy = 512;
    if ((i + 1) * 512 > bytes)
      copy = bytes - i * 512;
    memset(tmp, 0, 512);
    memcpy(tmp, p + i * 512, copy);
    if (disk_write_sector(lba + i, tmp) != 0) {
      serial("[INSTALLER] ERROR: disk_write_sector failed at LBA %u\n",
             lba + i);
      break;
    }
  }
  kfree(tmp);
}

static void build_mbr(uint8_t *mbr, const uint8_t *boot_img, uint32_t start_lba,
                      uint32_t total_sectors) {
  memset(mbr, 0, 512);
  if (boot_img) {
    memcpy(mbr, boot_img, 446); /* boot code only */
  }
  /* Partition entry 0 */
  uint8_t *p = mbr + 446;
  p[0] = 0x80; /* bootable */
  p[1] = 0;
  p[2] = 0;
  p[3] = 0;    /* CHS start (unused) */
  p[4] = 0x0C; /* FAT32 LBA */
  p[5] = 0;
  p[6] = 0;
  p[7] = 0; /* CHS end (unused) */
  p[8] = (uint8_t)(start_lba & 0xFF);
  p[9] = (uint8_t)((start_lba >> 8) & 0xFF);
  p[10] = (uint8_t)((start_lba >> 16) & 0xFF);
  p[11] = (uint8_t)((start_lba >> 24) & 0xFF);
  uint32_t count =
      (total_sectors > start_lba) ? (total_sectors - start_lba) : 0;
  p[12] = (uint8_t)(count & 0xFF);
  p[13] = (uint8_t)((count >> 8) & 0xFF);
  p[14] = (uint8_t)((count >> 16) & 0xFF);
  p[15] = (uint8_t)((count >> 24) & 0xFF);
  mbr[510] = 0x55;
  mbr[511] = 0xAA;
}

/* Keyboard polling for menu */
static uint8_t kbd_getscancode() {
  while (!(inb(0x64) & 1))
    ; // wait for output buffer full
  return inb(0x60);
}

static char kbd_getchar() {
  uint8_t scancode = kbd_getscancode();
  if (scancode & 0x80)
    return 0; // ignore release
  static const char map[] = {
      0,   27,  '1',  '2',  '3',  '4', '5', '6',  '7', '8', '9', '0',
      '-', '=', '\b', '\t', 'q',  'w', 'e', 'r',  't', 'y', 'u', 'i',
      'o', 'p', '[',  ']',  '\n', 0,   'a', 's',  'd', 'f', 'g', 'h',
      'j', 'k', 'l',  ';',  '\'', '`', 0,   '\\', 'z', 'x', 'c', 'v',
      'b', 'n', 'm',  ',',  '.',  '/', 0,   '*',  0,   ' '};
  if (scancode < sizeof(map))
    return map[scancode];
  return 0;
}

/* Simple input helper */
static void get_input(char *buf, int max) {
  int i = 0;
  while (i < max - 1) {
    char c = kbd_getchar();
    if (c == '\n') {
      break;
    } else if (c == '\b') {
      if (i > 0) {
        i--;
        terminal_putentryat(' ', 0x1F, terminal_get_cursor_x() - 1,
                            terminal_get_cursor_y());
        terminal_set_cursor_pos(terminal_get_cursor_x() - 1,
                                terminal_get_cursor_y());
      }
    } else if (c >= 32 && c <= 126) {
      buf[i++] = c;
      terminal_putchar(c);
    }
  }
  buf[i] = 0;
}

static void get_password(char *buf, int max) {
  int i = 0;
  while (i < max - 1) {
    char c = kbd_getchar();
    if (c == '\n') {
      break;
    } else if (c == '\b') {
      if (i > 0) {
        i--;
        terminal_putentryat(' ', 0x1F, terminal_get_cursor_x() - 1,
                            terminal_get_cursor_y());
        terminal_set_cursor_pos(terminal_get_cursor_x() - 1,
                                terminal_get_cursor_y());
      }
    } else if (c >= 32 && c <= 126) {
      buf[i++] = c;
      terminal_putchar('*');
    }
  }
  buf[i] = 0;
}

static void recovery_shell() {
  terminal_clear();
  terminal_set_color(0x07); // Light Grey on Black
  terminal_printf("Chrysalis OS Recovery Shell\n");
  terminal_printf("Commands: ls, cd, reboot, shutdown, exit\n\n");

  char buf[128];
  char *argv[16];

  while (1) {
    char cwd[256];
    cd_get_cwd(cwd, 256);
    terminal_printf("Shell:%s> ", cwd);

    get_input(buf, 128);
    terminal_printf("\n");

    if (strlen(buf) == 0)
      continue;

    // Tokenize
    int argc = 0;
    char *p = buf;
    while (*p) {
      while (*p == ' ')
        p++;
      if (!*p)
        break;
      argv[argc++] = p;
      while (*p && *p != ' ')
        p++;
      if (*p)
        *p++ = 0;
      if (argc >= 16)
        break;
    }

    if (argc > 0) {
      if (strcmp(argv[0], "exit") == 0) {
        return;
      } else if (strcmp(argv[0], "ls") == 0) {
        cmd_ls(argc, argv);
      } else if (strcmp(argv[0], "cd") == 0) {
        cmd_cd(argc, argv);
      } else if (strcmp(argv[0], "reboot") == 0) {
        cmd_reboot(NULL);
      } else if (strcmp(argv[0], "shutdown") == 0) {
        cmd_shutdown(NULL);
      } else if (strcmp(argv[0], "help") == 0) {
        terminal_printf("Available commands:\n");
        terminal_printf("  ls [dir]        - List directory\n");
        terminal_printf("  cd [dir]        - Change directory\n");
        terminal_printf("  reboot          - Reboot system\n");
        terminal_printf("  shutdown        - Shutdown system\n");
        terminal_printf("  exit            - Return to installer\n");
      } else {
        terminal_printf("Unknown command: %s\n", argv[0]);
      }
    }
  }
}

extern "C" void installer_main(uint32_t magic, uint32_t addr) {
  (void)magic;
  kmalloc_init();

  terminal_set_color(0x1F); // Blue background, White text
  terminal_clear();

  serial_init();
  serial("\n\n[INSTALLER] Starting Chrysalis OS Installer...\n");

  /* 0. Welcome Menu */
  terminal_set_color(0x1F);
  terminal_clear();

  // Header bar
  terminal_set_color(0x70); // Black on Light Grey
  for (int x = 0; x < 80; x++)
    terminal_putentryat(' ', 0x70, x, 0);
  terminal_printf(" Chrysalis OS Setup v1.0                                    "
                  "       [Installer]");

  terminal_set_color(0x1F);
  terminal_printf("\n\n\n");
  terminal_printf(
      "    ************************************************************\n");
  terminal_printf(
      "    *                                                          *\n");
  terminal_printf(
      "    *              Welcome to Chrysalis OS Setup               *\n");
  terminal_printf(
      "    *                                                          *\n");
  terminal_printf(
      "    ************************************************************\n\n");

  terminal_printf("    This program will install or upgrade Chrysalis OS on "
                  "your computer.\n\n");
  terminal_printf("    Please choose an option:\n\n");
  terminal_printf(
      "    [1] Fresh Install  - Wipes the disk and installs a new system.\n");
  terminal_printf("    [2] Upgrade        - Keeps your files and updates "
                  "system components.\n");
  terminal_printf("    [0] Shutdown       - Shuts down the system.\n");
  terminal_printf("    [J] Recovery Shell - Opens a command shell.\n\n");
  terminal_printf("\n\n\n\n\n\n\n\n\n");

  // Footer bar
  terminal_set_color(0x70);
  for (int x = 0; x < 80; x++)
    terminal_putentryat(' ', 0x70, x, 24);
  terminal_printf(" [1,2,0,J] Select Option    [F3] Exit");

  terminal_set_color(0x1F);

  char choice = 0;
  while (choice != '1' && choice != '2' && choice != '0' && choice != 'J' &&
         choice != 'j') {
    choice = kbd_getchar();
  }

  if (choice == '0') {
    serial("[INSTALLER] Shutting down...\n");
    cmd_shutdown(NULL);
    return;
  }

  if (choice == 'J' || choice == 'j') {
    recovery_shell();
    // After exit, restart main (recursive or loop? safer to just return to
    // allow main loop if structure supports it, but here we are in main. Let's
    // restart main logic by using a goto or loop.) Actually easier to just
    // recursively call installer_main or jump to start. Jumping to start of
    // installer_main is hard without a label. Let's just return, and in the
    // loader we define what happens? No, the loader hangs. I should put the
    // menu in a loop. For now, let's just loop back to menu by recursive call
    // (stack depth is fine for a few tries) or better: refactor main to have a
    // loop. Refactoring is cleaner. I will wrap the menu in a `while(true)`

    // Since I can't easily refactor the whole function in this block,
    // I will use `installer_main(magic, addr); return;` which is recursion.
    // It's safe enough for a recovery shell option.
    installer_main(magic, addr);
    return;
  }
  serial("> Choice: %c selected.\n\n", choice);

  bool upgrade_mode = (choice == '2');

  // Clear for progress
  terminal_clear();
  terminal_set_color(0x70);
  for (int x = 0; x < 80; x++)
    terminal_putentryat(' ', 0x70, x, 0);
  terminal_printf(" Chrysalis OS %s in progress...",
                  upgrade_mode ? "Upgrade" : "Installation");
  terminal_set_color(0x1F);
  terminal_printf("\n\n");

  /* 1. Initialize Subsystems */
  kmalloc_init();

  serial("[INSTALLER] Initializing ATA...\n");
  ata_init();
  /* Keep cache flush enabled for verified writes */
  ata_set_skip_cache_flush(0);

  /* 2. Format / Prepare Target Disk */
  ata_set_allow_mbr_write(1);

  uint32_t start_lba = 2048;
  uint32_t total_sectors = disk_get_capacity();
  if (total_sectors == 0)
    total_sectors = 262144; // Default 128MB

  if (!upgrade_mode) {
    serial("[INSTALLER] Action: Formatting Partition 1 (LBA %d)...\n",
           start_lba);
    if (fat32_format(start_lba, total_sectors - start_lba, "CHRYSALIS") != 0) {
      serial("[INSTALLER] ERROR: Formatting failed!\n");
      return;
    }
    serial("[INSTALLER] Format Complete.\n");
  } else {
    serial("[INSTALLER] Action: UPGRADE (Verifying existing Filesystem at LBA "
           "%d)...\n",
           start_lba);
  }

  /* 2.1 User Setup (Only for Fresh Install) */
  char username[32];
  char password[32];
  char hostname[32];

  if (!upgrade_mode) {
    terminal_clear();
    terminal_set_color(0x70);
    for (int x = 0; x < 80; x++)
      terminal_putentryat(' ', 0x70, x, 0);
    terminal_printf(
        " Chrysalis OS - User Setup                                    "
        "             ");
    terminal_set_color(0x1F);
    terminal_printf("\n\n");

    terminal_printf("    Please create your user account:\n\n");

    terminal_printf("    Username: ");
    get_input(username, 32);
    terminal_printf("\n");

    terminal_printf("    Password: ");
    get_password(password, 32);
    terminal_printf("\n");

    terminal_printf("    Device Name: ");
    get_input(hostname, 32);
    terminal_printf("\n");

    serial("[INSTALLER] User setup: user='%s' host='%s'\n", username, hostname);
  } else {
    serial("[INSTALLER] Skipping user setup (upgrade mode - preserving "
           "existing users)\n");
  }

  terminal_clear();
  terminal_set_color(0x70);
  for (int x = 0; x < 80; x++)
    terminal_putentryat(' ', 0x70, x, 0);
  terminal_printf(" Chrysalis OS %s in progress...",
                  upgrade_mode ? "Upgrade" : "Installation");
  terminal_set_color(0x1F);
  terminal_printf("\n\n");
  if (fat32_init(0, start_lba) != 0) {
    serial("[INSTALLER] Search failed at LBA %d, checking LBA 0...\n",
           start_lba);
    if (fat32_init(0, 0) != 0) {
      serial("[INSTALLER] ERROR: No FAT32 filesystem found. You must use [1] "
             "Fresh Install.\n");
      return;
    }
    fat32_set_mounted(0, 'a');
  } else {
    fat32_set_mounted(start_lba, 'a');
  }

  /* 2.5 Repair phase (Deduplicate system dirs) */
  if (upgrade_mode) {
    serial("[INSTALLER] Repair Phase: Cleaning root directory duplicates...\n");
    fat32_deduplicate_root();
  }

  /* Dump BPB layout for debug */
  uint8_t *bpb_dbg = (uint8_t *)kmalloc(512);
  if (bpb_dbg && disk_read_sector(start_lba, bpb_dbg) == 0) {
    struct fat_bpb *bpb = (struct fat_bpb *)bpb_dbg;
    uint32_t fat_start = start_lba + bpb->reserved_sectors;
    uint32_t data_start =
        fat_start + (bpb->fats_count * bpb->sectors_per_fat_32);
    serial(
        "[INSTALLER] BPB: total_sectors=%u spc=%u fat_start=%u data_start=%u\n",
        bpb->total_sectors_32, bpb->sectors_per_cluster, fat_start, data_start);
    kfree(bpb_dbg);
  } else if (bpb_dbg) {
    kfree(bpb_dbg);
  }

  /* 3. Create Directory Structure */
  serial("[INSTALLER] Creating directories...\n");
  char boot_path[6] = {'/', 'b', 'o', 'o', 't', 0};
  char chrys_path[16] = {'/', 'b', 'o', 'o', 't', '/', 'c', 'h',
                         'r', 'y', 's', 'a', 'l', 'i', 's', 0};
  char grub_path[11] = {'/', 'b', 'o', 'o', 't', '/', 'g', 'r', 'u', 'b', 0};
  char system_path[8] = {'/', 's', 'y', 's', 't', 'e', 'm', 0};
  char icons_dir[14] = {'/', 's', 'y', 's', 't', 'e', 'm',
                        '/', 'i', 'c', 'o', 'n', 's', 0};
  char themes_dir[18] = {'/', 'b', 'o', 'o', 't', '/', 'g', 'r', 'u',
                         'b', '/', 't', 'h', 'e', 'm', 'e', 's', 0};
  char theme_dir[28] = {'/', 'b', 'o', 'o', 't', '/', 'g', 'r', 'u', 'b',
                        '/', 't', 'h', 'e', 'm', 'e', 's', '/', 'c', 'h',
                        'r', 'y', 's', 'a', 'l', 'i', 's', 0};

  int mr = fat32_create_directory_verified(boot_path, 1);
  if (mr != 0 && !upgrade_mode) {
    serial("[INSTALLER] ERROR: mkdir /boot failed (err=%d)\n", mr);
    return;
  }
  mr = fat32_create_directory_verified(chrys_path, 1);
  if (mr != 0 && !upgrade_mode) {
    serial("[INSTALLER] ERROR: mkdir /boot/chrysalis failed (err=%d)\n", mr);
    return;
  }
  mr = fat32_create_directory_verified(grub_path, 1);
  if (mr != 0 && !upgrade_mode) {
    serial("[INSTALLER] ERROR: mkdir /boot/grub failed (err=%d)\n", mr);
    return;
  }
  mr = fat32_create_directory_verified(system_path, 1);
  if (mr != 0 && !upgrade_mode) {
    serial("[INSTALLER] WARN: mkdir /system failed (err=%d), continuing\n", mr);
  }
  mr = fat32_create_directory_verified(icons_dir, 1);
  if (mr != 0 && !upgrade_mode) {
    serial("[INSTALLER] WARN: mkdir /system/icons failed (err=%d)\n", mr);
  }
  mr = fat32_create_directory_verified(themes_dir, 1);
  if (mr != 0 && !upgrade_mode) {
    serial("[INSTALLER] WARN: mkdir /boot/grub/themes failed (err=%d)\n", mr);
  }
  mr = fat32_create_directory_verified(theme_dir, 1);
  if (mr != 0 && !upgrade_mode) {
    serial(
        "[INSTALLER] WARN: mkdir /boot/grub/themes/chrysalis failed (err=%d)\n",
        mr);
  }

  /* Directory listings disabled to reduce stack usage and avoid instability */

  /* 4. Locate Source Files (Multiboot Modules) */
  void *kernel_data = NULL;
  size_t kernel_size = 0;
  void *boot_img = NULL;
  size_t boot_img_size = 0;
  void *core_img = NULL;
  size_t core_img_size = 0;
  void *icon_data[ICON_COUNT] = {0};
  size_t icon_sizes[ICON_COUNT] = {0};
  void *theme_txt_data = NULL;
  size_t theme_txt_size = 0;
  void *bg_tga_data = NULL;
  size_t bg_tga_size = 0;
  void *sel_tga_data = NULL;
  size_t sel_tga_size = 0;

  /* Scan multidoob tags (parsed manually here as we need raw addresses) */
  struct multiboot2_tag *tag = (struct multiboot2_tag *)(uintptr_t)(addr + 8);
  while (tag->type != MULTIBOOT2_TAG_TYPE_END) {
    if (tag->type == MULTIBOOT2_TAG_TYPE_MODULE) {
      struct multiboot2_tag_module *mod = (struct multiboot2_tag_module *)tag;
      const char *cmdline = mod->string; /* Correct field name: char string[] */

      serial("[INSTALLER] Found Module: '%s' start=%x end=%x size=%d\n",
             cmdline, mod->mod_start, mod->mod_end,
             (int)(mod->mod_end - mod->mod_start));

      if (cmdline) {
        size_t cmd_len = 0;
        if (tag->size > sizeof(struct multiboot2_tag_module)) {
          cmd_len = tag->size - sizeof(struct multiboot2_tag_module);
        }
        if (cmd_len > 0 && cmdline[cmd_len - 1] == '\0') {
          cmd_len--;
        }
        char mod_name[64];
        normalize_module_name_len(cmdline, cmd_len, mod_name, sizeof(mod_name));
        serial("[INSTALLER] Module name parsed: '%s'\n", mod_name);

        int m_kernel = strcmp(mod_name, "kernel.bin");
        int m_boot = strcmp(mod_name, "boot.img");
        int m_core = strcmp(mod_name, "core.img");
        serial("[INSTALLER] mod_name cmp: kernel=%d boot=%d core=%d\n",
               m_kernel, m_boot, m_core);

        if (m_kernel == 0) {
          kernel_data = (void *)(uintptr_t)mod->mod_start;
          kernel_size = mod->mod_end - mod->mod_start;
          serial("[INSTALLER] Assigned to kernel.bin\n");
        } else if (m_boot == 0) {
          boot_img = (void *)(uintptr_t)mod->mod_start;
          boot_img_size = mod->mod_end - mod->mod_start;
          serial("[INSTALLER] Assigned to boot.img\n");
        } else if (m_core == 0) {
          core_img = (void *)(uintptr_t)mod->mod_start;
          core_img_size = mod->mod_end - mod->mod_start;
          serial("[INSTALLER] Assigned to core.img\n");
        } else if (strcmp(mod_name, "theme.txt") == 0) {
          theme_txt_data = (void *)(uintptr_t)mod->mod_start;
          theme_txt_size = mod->mod_end - mod->mod_start;
          serial("[INSTALLER] Assigned to theme.txt\n");
        } else if (strcmp(mod_name, "background.tga") == 0) {
          bg_tga_data = (void *)(uintptr_t)mod->mod_start;
          bg_tga_size = mod->mod_end - mod->mod_start;
          serial("[INSTALLER] Assigned to background.tga\n");
        } else if (strcmp(mod_name, "select_c.tga") == 0) {
          sel_tga_data = (void *)(uintptr_t)mod->mod_start;
          sel_tga_size = mod->mod_end - mod->mod_start;
          serial("[INSTALLER] Assigned to select_c.tga\n");
        } else {
          /* Try BMP icons */
          for (int i = 0; i < ICON_COUNT; i++) {
            if (strcmp(mod_name, g_icon_files[i]) == 0) {
              icon_data[i] = (void *)(uintptr_t)mod->mod_start;
              icon_sizes[i] = mod->mod_end - mod->mod_start;
              serial("[INSTALLER] Assigned to %s\n", g_icon_files[i]);
              goto mod_done;
            }
          }
          serial("[INSTALLER] Module '%s' did not match any expected file.\n",
                 cmdline);
        }
      mod_done:;
      }
    }
    tag = (struct multiboot2_tag *)((uint8_t *)tag + ((tag->size + 7) & ~7));
  }

  /* 4.1 Install GRUB boot code + core.img */
  serial("[INSTALLER] GRUB assets: boot_img=%x size=%d core_img=%x size=%d\n",
         (uint32_t)(uintptr_t)boot_img, (int)boot_img_size,
         (uint32_t)(uintptr_t)core_img, (int)core_img_size);
  if (!boot_img || boot_img_size < 446 || !core_img || core_img_size == 0) {
    serial(
        "[INSTALLER] ERROR: GRUB boot/core images missing in installer ISO.\n");
    return;
  }

  uint32_t core_sectors = (core_img_size + 511) / 512;
  if (1 + core_sectors >= start_lba) {
    serial("[INSTALLER] ERROR: core.img too large (%u sectors)\n",
           core_sectors);
    return;
  }

  uint8_t *mbr = (uint8_t *)kmalloc(512);
  if (!mbr) {
    serial("[INSTALLER] ERROR: no memory for MBR\n");
    return;
  }
  build_mbr(mbr, (const uint8_t *)boot_img, start_lba, total_sectors);
  disk_write_sector(0, mbr);
  kfree(mbr);

  serial("[INSTALLER] Writing GRUB core.img (%d bytes)...\n",
         (int)core_img_size);
  write_sectors(1, core_img, (uint32_t)core_img_size);

  /* 5. Write GRUB config */
  const char *grub_cfg = "# Load graphical modules\n"
                         "insmod png\n"
                         "insmod tga\n"
                         "insmod jpeg\n"
                         "insmod font\n"
                         "insmod gfxterm\n"
                         "insmod vbe\n"
                         "insmod vga\n"
                         "insmod all_video\n"
                         "\n"
                         "# Set graphics mode\n"
                         "set gfxmode=1024x768,800x600,auto\n"
                         "set gfxpayload=keep\n"
                         "\n"
                         "# Enable graphical terminal\n"
                         "terminal_output gfxterm\n"
                         "\n"
                         "# Set theme\n"
                         "set theme=/boot/grub/themes/chrysalis/theme.txt\n"
                         "\n"
                         "set timeout=5\n"
                         "set default=0\n"
                         "\n"
                         "menuentry \"Chrysalis OS\" {\n"
                         "  multiboot2 /boot/chrysalis/kernel.bin\n"
                         "  module2 /system/icons/start.bmp start.bmp\n"
                         "  module2 /system/icons/term.bmp term.bmp\n"
                         "  module2 /system/icons/files.bmp files.bmp\n"
                         "  module2 /system/icons/img.bmp img.bmp\n"
                         "  module2 /system/icons/note.bmp note.bmp\n"
                         "  module2 /system/icons/paint.bmp paint.bmp\n"
                         "  module2 /system/icons/calc.bmp calc.bmp\n"
                         "  module2 /system/icons/clock.bmp clock.bmp\n"
                         "  module2 /system/icons/task.bmp task.bmp\n"
                         "  module2 /system/icons/info.bmp info.bmp\n"
                         "  module2 /system/icons/3D.bmp 3D.bmp\n"
                         "  module2 /system/icons/mine.bmp mine.bmp\n"
                         "  module2 /system/icons/net.bmp net.bmp\n"
                         "  module2 /system/icons/x0.bmp x0.bmp\n"
                         "  module2 /system/icons/run.bmp run.bmp\n"
                         "  boot\n"
                         "}\n"
                         "menuentry \"Chrysalis OS (PIC Safe)\" {\n"
                         "  multiboot2 /boot/chrysalis/kernel.bin apic=off\n"
                         "  boot\n"
                         "}\n";
  serial("[INSTALLER] Writing GRUB configuration...\n");
  fat32_create_file_verified("/boot/grub/grub.cfg", grub_cfg, strlen(grub_cfg),
                             1);
  fat32_create_file_verified("/grub.cfg", grub_cfg, strlen(grub_cfg), 1);

  /* Install core.img into /boot/grub for normal GRUB stage2 lookup */
  serial("[INSTALLER] Copying core.img to /boot/grub/core.img...\n");
  fat32_create_file_verified("/boot/grub/core.img", core_img,
                             (uint32_t)core_img_size, 1);

  /* Write theme files */
  if (theme_txt_data && theme_txt_size > 0) {
    serial("[INSTALLER] Installing theme.txt...\n");
    fat32_create_file_verified("/boot/grub/themes/chrysalis/theme.txt",
                               theme_txt_data, (uint32_t)theme_txt_size, 1);
  }
  if (bg_tga_data && bg_tga_size > 0) {
    serial("[INSTALLER] Installing background.tga...\n");
    fat32_create_file_verified("/boot/grub/themes/chrysalis/background.tga",
                               bg_tga_data, (uint32_t)bg_tga_size, 1);
  }
  if (sel_tga_data && sel_tga_size > 0) {
    serial("[INSTALLER] Installing select_c.tga...\n");
    fat32_create_file_verified("/boot/grub/themes/chrysalis/select_c.tga",
                               sel_tga_data, (uint32_t)sel_tga_size, 1);
  }

  /* 6. Install Kernel (chunked) */
  if (kernel_data && kernel_size > 0) {
    serial("[INSTALLER] Installing Kernel (%d bytes)...\n", (int)kernel_size);
    int r = fat32_create_file_alloc("/boot/chrysalis/kernel.bin", kernel_size);
    if (r != 0) {
      serial("[INSTALLER] ERROR: Failed to allocate kernel.bin (err=%d)\n", r);
      return;
    }
    const uint8_t *kp = (const uint8_t *)kernel_data;
    uint32_t chunk = 64 * 1024;
    uint32_t offset = 0;
    while (offset < kernel_size) {
      uint32_t n = kernel_size - offset;
      if (n > chunk)
        n = chunk;
      int wr = fat32_write_file_offset("/boot/chrysalis/kernel.bin",
                                       kp + offset, n, offset, 0);
      if (wr != 0) {
        serial("[INSTALLER] ERROR: kernel.bin chunk write failed (off=%d "
               "err=%d)\n",
               (int)offset, wr);
        return;
      }
      offset += n;
      serial("[INSTALLER] kernel.bin progress %d/%d\n", (int)offset,
             (int)kernel_size);
    }
    serial("[INSTALLER] Kernel Installed OK.\n");
    int32_t ksz = fat32_get_file_size("/boot/chrysalis/kernel.bin");
    serial("[INSTALLER] kernel.bin size on disk: %d\n", ksz);
    if (ksz != (int32_t)kernel_size) {
      serial("[INSTALLER] ERROR: kernel.bin size mismatch\n");
      return;
    }
  } else {
    serial("[INSTALLER] ERROR: Kernel module not found in memory!\n");
    return;
  }

  /* 7. Install Icons (BMP files) */
  serial("[INSTALLER] Installing Icons (BMP)...\n");
  int icons_written = 0;
  for (int i = 0; i < ICON_COUNT; i++) {
    if (!icon_data[i] || icon_sizes[i] == 0)
      continue;
    char path[48];
    /* "/system/icons/<name>" */
    path[0] = '/';
    path[1] = 's';
    path[2] = 'y';
    path[3] = 's';
    path[4] = 't';
    path[5] = 'e';
    path[6] = 'm';
    path[7] = '/';
    path[8] = 'i';
    path[9] = 'c';
    path[10] = 'o';
    path[11] = 'n';
    path[12] = 's';
    path[13] = '/';
    const char *nm = g_icon_files[i];
    int j = 0;
    while (nm[j] && (14 + j) < (int)sizeof(path) - 1) {
      path[14 + j] = nm[j];
      j++;
    }
    path[14 + j] = 0;

    serial("[INSTALLER] icon %s (%d bytes) -> %s\n", nm, (int)icon_sizes[i],
           path);
    int r = fat32_create_file_verified(path, icon_data[i],
                                       (uint32_t)icon_sizes[i], 0);
    if (r != 0) {
      serial("[INSTALLER] ERROR: icon write failed (%s err=%d)\n", nm, r);
      return;
    }
    icons_written++;
    kmalloc_reset();
  }
  serial("[INSTALLER] Icons Installed OK (%d files).\n", icons_written);

  serial("[INSTALLER] Icons Installed OK (%d files).\n", icons_written);

  /* 7.1 Create User Data (Only for Fresh Install) */
  if (!upgrade_mode) {
    char users_dir[32] = "/system/users";
    fat32_create_directory_verified(users_dir, 1);

    // char user_home[64];
    // user_home[0] = 0; /* Just a placeholder */
    // snprintf not available, manually build path
    int ulen = strlen(username);
    if (ulen > 0) {
      char udir[64];
      // /system/users/USERNAME
      memcpy(udir, users_dir, 13);
      udir[13] = '/';
      memcpy(udir + 14, username, ulen);
      udir[14 + ulen] = 0;

      fat32_create_directory_verified(udir, 1);

      // Hash password
      uint8_t hash[32];
      sha256_ctx_t sha;
      sha256_init(&sha);
      sha256_update(&sha, (const uint8_t *)password, strlen(password));
      sha256_final(&sha, hash);

      char hash_hex[65];
      const char *hex = "0123456789abcdef";
      for (int i = 0; i < 32; i++) {
        hash_hex[i * 2] = hex[(hash[i] >> 4) & 0xF];
        hash_hex[i * 2 + 1] = hex[hash[i] & 0xF];
      }
      hash_hex[64] = 0;

      // Create data.json
      char json_path[128];
      memcpy(json_path, udir, strlen(udir));
      memcpy(json_path + strlen(udir), "/data.json", 11); // 10 chars + null
      json_path[strlen(udir) + 10] = 0;

      // Build JSON content manually
      char json_content[512];
      // Format: {"username":"U","password":"H","device-name":"D"}
      // We don't have sprintf, so we construct it carefully or use
      // terminal_printf trick (no, cannot redirect). Let's implement a
      // mini-copy.

      // We will perform exact copy for simplicity
      // Note: simplistic, assumes no escaping needed.
      char *p = json_content;
      auto append = [&](const char *s) {
        while (*s)
          *p++ = *s++;
      };

      append("{\n  \"username\": \"");
      append(username);
      append("\",\n  \"password\": \"");
      append(hash_hex);
      append("\",\n  \"device-name\": \"");
      append(hostname);
      append("\"\n}\n");
      *p = 0;

      fat32_create_file_verified(json_path, json_content,
                                 (uint32_t)(p - json_content), 1);
      serial("[INSTALLER] User data written to %s\n", json_path);
    }
  } else {
    serial("[INSTALLER] Skipping user data creation (upgrade mode - preserving "
           "existing users)\n");
  }

  int32_t gsz = fat32_get_file_size("/boot/grub/grub.cfg");
  serial("[INSTALLER] grub.cfg size: %d\n", gsz);

  fat_file_info_t c_entries[16];
  int c_count = fat32_read_directory("/boot/chrysalis", c_entries, 16);
  serial("[INSTALLER] /boot/chrysalis entries: %d\n", c_count);
  for (int i = 0; i < c_count; i++) {
    serial("  %s%s (%u)\n", c_entries[i].name, c_entries[i].is_dir ? "/" : "",
           c_entries[i].size);
  }

  serial("\n[INSTALLER] Installation Complete.\n");

  /* 8. Success Screen */
  while (true) {
    terminal_clear();
    terminal_set_color(0x70);
    for (int x = 0; x < 80; x++)
      terminal_putentryat(' ', 0x70, x, 0);
    terminal_printf(" Chrysalis OS %s Complete                                 "
                    "        [Success]",
                    upgrade_mode ? "Upgrade" : "Installation");

    terminal_set_color(0x1F);
    terminal_printf("\n\n\n");
    terminal_printf(
        "    ************************************************************\n");
    terminal_printf(
        "    *                                                          *\n");
    terminal_printf(
        "    *             ChrysalisOS installed successfully            *\n");
    terminal_printf(
        "    *                                                          *\n");
    terminal_printf(
        "    ************************************************************\n\n");

    terminal_printf("    Version  = 0.2\n");
    terminal_printf("    Website  = https://chrysalisos.netlify.app\n\n");
    terminal_printf(
        "    The installation has finished. Please choose how to proceed:\n\n");

    terminal_printf("    [M] Shutdown  - Safely turn off the computer.\n");
    terminal_printf("    [R] Reboot    - Restart to boot into Chrysalis OS.\n");
    terminal_printf(
        "    [S] Shell     - Drop into a minimal recovery shell.\n\n");

    terminal_printf("\n\n\n\n\n\n");

    // Footer bar
    terminal_set_color(0x70);
    for (int x = 0; x < 80; x++)
      terminal_putentryat(' ', 0x70, x, 24);
    terminal_printf(" [M,R,S] Select Action");
    terminal_set_color(0x1F);

    bool back_to_menu = false;
    while (!back_to_menu) {
      char fc = kbd_getchar();
      if (fc == 'm' || fc == 'M') {
        serial("[INSTALLER] Shutdown requested.\n");
        outw(0x604, 0x2000);  // QEMU
        outw(0x4004, 0x3400); // VBox
        outw(0xB004, 0x2000); // Bochs
      } else if (fc == 'r' || fc == 'R') {
        serial("[INSTALLER] Rebooting...\n");
        outb(0x64, 0xFE);
      } else if (fc == 's' || fc == 'S') {
        // Simple shell placeholder
        terminal_clear();
        terminal_set_color(0x07);
        terminal_printf("Chrysalis OS Installer Minimal Recovery Shell\n");
        terminal_printf(
            "Type 'reboot' to restart, or 'exit' to return to menu.\n\n# ");

        char line[64];
        int pos = 0;
        bool in_shell = true;
        while (in_shell) {
          char c = kbd_getchar();
          if (c == '\n') {
            line[pos] = 0;
            terminal_printf("\n");
            if (pos > 0) {
              if (strcmp(line, "reboot") == 0)
                outb(0x64, 0xFE);
              else if (strcmp(line, "exit") == 0)
                in_shell = false;
              else if (strcmp(line, "help") == 0)
                terminal_printf(
                    "Available commands: help, reboot, exit, version\n");
              else if (strcmp(line, "version") == 0)
                terminal_printf("Chrysalis OS Installer v1.0 (Kernel 0.2)\n");
              else
                terminal_printf("Unknown command: %s\n", line);
            }
            if (in_shell)
              terminal_printf("# ");
            pos = 0;
          } else if (c == '\b') {
            if (pos > 0) {
              pos--;
              terminal_printf("\b \b");
            }
          } else if (c >= 32 && pos < 63) {
            line[pos++] = c;
            terminal_putchar(c);
          }
        }
        back_to_menu = true;
      }
    }
  }
}

// String functions provided by string.cpp
