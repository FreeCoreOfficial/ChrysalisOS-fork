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
#include "../os/kernel/input/input.h"
#include "../os/kernel/usb/usb_core.h"

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
void serial_set_vga_mirror(int enabled);
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

/* Forward declarations for menu/input helpers used below */
static char kbd_getchar();

static bool has_extension(const char *name, const char *ext) {
  size_t nl = strlen(name);
  size_t el = strlen(ext);
  if (nl < el)
    return false;
  return strcmp(name + nl - el, ext) == 0;
}

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

enum installer_os_hint_t {
  INSTALLER_OS_NONE = 0,
  INSTALLER_OS_CHRYSALIS = 1,
  INSTALLER_OS_WINDOWS = 2,
  INSTALLER_OS_LINUX = 3,
  INSTALLER_OS_EFI = 4,
  INSTALLER_OS_OTHER = 5
};

struct installer_partition_info_t {
  bool present;
  uint8_t bootable;
  uint8_t type;
  uint32_t lba;
  uint32_t count;
  int os_hint;
};

static int g_install_target_part_index = -1;
static uint32_t g_install_target_lba = 2048;
static uint32_t g_install_target_count = 0;
static int g_install_target_os_hint = INSTALLER_OS_NONE;

static const uint32_t INSTALLER_DEFAULT_START_LBA = 2048;
static const uint32_t INSTALLER_MIN_PARTITION_SECTORS = 131072; /* 64 MiB */

static uint32_t installer_read_u32_le(const uint8_t *p) {
  return ((uint32_t)p[0]) | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) |
         ((uint32_t)p[3] << 24);
}

static void installer_write_u32_le(uint8_t *p, uint32_t v) {
  p[0] = (uint8_t)(v & 0xFF);
  p[1] = (uint8_t)((v >> 8) & 0xFF);
  p[2] = (uint8_t)((v >> 16) & 0xFF);
  p[3] = (uint8_t)((v >> 24) & 0xFF);
}

static uint32_t installer_part_size_mb(uint32_t sectors) {
  return (sectors * 512U) / (1024U * 1024U);
}

static const char *installer_part_type_name(uint8_t type) {
  switch (type) {
  case 0x01:
    return "FAT12";
  case 0x04:
  case 0x06:
    return "FAT16";
  case 0x05:
  case 0x0F:
  case 0x85:
    return "Extended";
  case 0x07:
    return "NTFS/exFAT";
  case 0x0B:
  case 0x0C:
    return "FAT32";
  case 0x82:
    return "Linux Swap";
  case 0x83:
    return "Linux";
  case 0x8E:
    return "Linux LVM";
  case 0xA5:
    return "BSD";
  case 0xAF:
    return "Apple HFS";
  case 0xEE:
    return "GPT Protective";
  case 0xEF:
    return "EFI System";
  default:
    return "Unknown";
  }
}

static const char *installer_os_hint_name(int hint) {
  switch (hint) {
  case INSTALLER_OS_CHRYSALIS:
    return "Chrysalis";
  case INSTALLER_OS_WINDOWS:
    return "Windows";
  case INSTALLER_OS_LINUX:
    return "Linux";
  case INSTALLER_OS_EFI:
    return "EFI/Boot";
  case INSTALLER_OS_OTHER:
    return "Other";
  default:
    return "None";
  }
}

static bool installer_fat32_boot_sector_is_valid(uint32_t lba) {
  uint8_t sec[512];
  if (disk_read_sector(lba, sec) != 0)
    return false;
  if (sec[510] != 0x55 || sec[511] != 0xAA)
    return false;

  uint16_t bps = (uint16_t)sec[11] | ((uint16_t)sec[12] << 8);
  uint8_t spc = sec[13];
  uint16_t reserved = (uint16_t)sec[14] | ((uint16_t)sec[15] << 8);
  uint8_t fats = sec[16];
  uint32_t spf32 = installer_read_u32_le(sec + 36);

  if (bps != 512)
    return false;
  if (spc == 0 || reserved == 0 || fats == 0 || spf32 == 0)
    return false;

  return true;
}

static int installer_detect_os_hint(const installer_partition_info_t &part) {
  if (!part.present || part.count == 0)
    return INSTALLER_OS_NONE;

  if (part.type == 0xEF || part.type == 0xEE)
    return INSTALLER_OS_EFI;
  if (part.type == 0x83 || part.type == 0x82 || part.type == 0x8E)
    return INSTALLER_OS_LINUX;

  uint8_t sec[512];
  if ((part.type == 0x07 || part.type == 0x17) &&
      disk_read_sector(part.lba, sec) == 0) {
    if (memcmp(sec + 3, "NTFS    ", 8) == 0)
      return INSTALLER_OS_WINDOWS;
  }

  if (part.type == 0x0B || part.type == 0x0C) {
    if (!installer_fat32_boot_sector_is_valid(part.lba)) {
      return INSTALLER_OS_NONE;
    }
    if (fat32_init(0, part.lba) == 0) {
      fat32_set_mounted(part.lba, 'a');
      if (fat32_get_file_size("/boot/chrysalis/kernel.bin") > 0)
        return INSTALLER_OS_CHRYSALIS;
      if (fat32_get_file_size("/EFI/Microsoft/Boot/bootmgfw.efi") > 0)
        return INSTALLER_OS_WINDOWS;
      return INSTALLER_OS_OTHER;
    }
  }

  return INSTALLER_OS_OTHER;
}

static int installer_scan_partitions(installer_partition_info_t out_parts[4],
                                     bool *out_has_valid_mbr, int *out_present,
                                     int *out_other_os, int *out_chrysalis) {
  if (out_has_valid_mbr)
    *out_has_valid_mbr = false;
  if (out_present)
    *out_present = 0;
  if (out_other_os)
    *out_other_os = 0;
  if (out_chrysalis)
    *out_chrysalis = 0;

  for (int i = 0; i < 4; i++) {
    out_parts[i].present = false;
    out_parts[i].bootable = 0;
    out_parts[i].type = 0;
    out_parts[i].lba = 0;
    out_parts[i].count = 0;
    out_parts[i].os_hint = INSTALLER_OS_NONE;
  }

  uint8_t mbr[512];
  if (disk_read_sector(0, mbr) != 0) {
    ata_init();
    if (disk_read_sector(0, mbr) != 0)
      return -1;
  }

  if (mbr[510] != 0x55 || mbr[511] != 0xAA)
    return 0;

  if (out_has_valid_mbr)
    *out_has_valid_mbr = true;

  for (int i = 0; i < 4; i++) {
    int off = 446 + i * 16;
    uint8_t type = mbr[off + 4];
    uint32_t lba = installer_read_u32_le(mbr + off + 8);
    uint32_t cnt = installer_read_u32_le(mbr + off + 12);

    if (type == 0 || cnt == 0)
      continue;

    out_parts[i].present = true;
    out_parts[i].bootable = (mbr[off] & 0x80) ? 1 : 0;
    out_parts[i].type = type;
    out_parts[i].lba = lba;
    out_parts[i].count = cnt;
    out_parts[i].os_hint = installer_detect_os_hint(out_parts[i]);

    if (out_present)
      (*out_present)++;
    if (out_parts[i].os_hint == INSTALLER_OS_CHRYSALIS) {
      if (out_chrysalis)
        (*out_chrysalis)++;
    } else if (out_parts[i].os_hint != INSTALLER_OS_NONE) {
      if (out_other_os)
        (*out_other_os)++;
    }
  }

  return 0;
}

static bool installer_confirm_yes_no(const char *line1, const char *line2) {
  terminal_printf("\n%s\n", line1 ? line1 : "");
  if (line2 && line2[0])
    terminal_printf("%s\n", line2);
  terminal_printf("Type [Y]es or [N]o: ");

  while (true) {
    char c = kbd_getchar();
    if (c == 'y' || c == 'Y') {
      terminal_printf("yes\n");
      return true;
    }
    if (c == 'n' || c == 'N' || c == 27) {
      terminal_printf("no\n");
      return false;
    }
  }
}

static int installer_create_single_fat32_layout(uint32_t total_sectors) {
  if (total_sectors <= INSTALLER_DEFAULT_START_LBA + 8192)
    return -1;

  uint8_t mbr[512];
  memset(mbr, 0, sizeof(mbr));

  uint8_t *p = mbr + 446;
  uint32_t count = total_sectors - INSTALLER_DEFAULT_START_LBA;

  p[0] = 0x80;
  p[4] = 0x0C;
  installer_write_u32_le(p + 8, INSTALLER_DEFAULT_START_LBA);
  installer_write_u32_le(p + 12, count);

  mbr[510] = 0x55;
  mbr[511] = 0xAA;

  ata_set_allow_mbr_write(1);
  int wr = disk_write_sector(0, mbr);
  ata_set_allow_mbr_write(0);
  if (wr != 0)
    return -1;

  uint8_t zero[512];
  memset(zero, 0, sizeof(zero));
  uint32_t wipe_count = (count > 64) ? 64 : count;
  for (uint32_t i = 0; i < wipe_count; i++) {
    if (disk_write_sector(INSTALLER_DEFAULT_START_LBA + i, zero) != 0)
      break;
  }
  return 0;
}

static int
installer_auto_select_partition(const installer_partition_info_t parts[4],
                                bool upgrade_mode) {
  int best_idx = -1;
  uint32_t best_count = 0;

  if (upgrade_mode) {
    for (int i = 0; i < 4; i++) {
      if (parts[i].present && parts[i].os_hint == INSTALLER_OS_CHRYSALIS)
        return i;
    }
  }

  for (int i = 0; i < 4; i++) {
    if (!parts[i].present)
      continue;
    if (parts[i].type == 0x05 || parts[i].type == 0x0F || parts[i].type == 0x85)
      continue;
    if (parts[i].count > best_count) {
      best_count = parts[i].count;
      best_idx = i;
    }
  }
  return best_idx;
}

static void installer_draw_partition_manager(
    const installer_partition_info_t parts[4], bool has_valid_mbr,
    uint32_t total_sectors, int selected_index, bool upgrade_mode,
    int other_os_count, int chrysalis_count, bool for_install_flow) {
  terminal_set_color(0x1F);
  terminal_clear();

  terminal_set_color(0x70);
  for (int x = 0; x < 80; x++)
    terminal_putentryat(' ', 0x70, x, 0);
  terminal_printf(" Partition Manager                                         "
                  "        [Installer]");

  terminal_set_color(0x1F);
  terminal_printf("\n\n");
  terminal_printf("    Disk capacity: %u sectors (%u MB)\n", total_sectors,
                  installer_part_size_mb(total_sectors));
  terminal_printf("    Mode: %s\n\n",
                  upgrade_mode ? "Upgrade" : "Fresh Install");

  if (!has_valid_mbr) {
    terminal_printf("    No valid MBR found. You can create a new layout.\n\n");
  } else {
    for (int i = 0; i < 4; i++) {
      if (!parts[i].present) {
        terminal_printf("    [%d] <empty>\n", i + 1);
        continue;
      }
      terminal_printf("    [%d] %c type=%s  start=%u  size=%uMB  os=%s%s\n",
                      i + 1, (parts[i].bootable ? '*' : ' '),
                      installer_part_type_name(parts[i].type), parts[i].lba,
                      installer_part_size_mb(parts[i].count),
                      installer_os_hint_name(parts[i].os_hint),
                      (selected_index == i) ? "  <target>" : "");
    }
    terminal_printf("\n");
  }

  terminal_printf("    OS detection: Chrysalis=%d, Other=%d\n", chrysalis_count,
                  other_os_count);
  if (selected_index >= 0 && selected_index < 4 &&
      parts[selected_index].present) {
    terminal_printf("    Current target: p%d (LBA %u, %u MB)\n",
                    selected_index + 1, parts[selected_index].lba,
                    installer_part_size_mb(parts[selected_index].count));
  }
  terminal_printf("\n");

  terminal_printf("    [1-4] Select partition");
  if (for_install_flow)
    terminal_printf("   [A] Auto");
  terminal_printf("\n");
  if (!upgrade_mode)
    terminal_printf(
        "    [C] Create single FAT32 layout (wipe partition table)\n");
  terminal_printf("    [R] Rescan   [B] Back\n");
}

static int installer_partition_manager_select(bool upgrade_mode,
                                              bool for_install_flow,
                                              int *out_index, uint32_t *out_lba,
                                              uint32_t *out_count,
                                              int *out_os_hint) {
  uint32_t total_sectors = disk_get_capacity();
  if (total_sectors == 0)
    total_sectors = 262144;

  int selected = -1;
  if (out_index && *out_index >= 0 && *out_index < 4)
    selected = *out_index;

  while (true) {
    installer_partition_info_t parts[4];
    bool has_valid_mbr = false;
    int present_count = 0;
    int other_os_count = 0;
    int chrysalis_count = 0;
    int rc = installer_scan_partitions(parts, &has_valid_mbr, &present_count,
                                       &other_os_count, &chrysalis_count);
    if (rc != 0) {
      terminal_set_color(0x1F);
      terminal_clear();
      terminal_printf("Partition scan failed.\n");
      terminal_printf("Press [B] to go back or [R] to retry.\n");
      while (true) {
        char c = kbd_getchar();
        if (c == 'b' || c == 'B' || c == 27)
          return -1;
        if (c == 'r' || c == 'R')
          break;
      }
      continue;
    }

    if (selected >= 0 && selected < 4 && !parts[selected].present)
      selected = -1;
    if (selected < 0)
      selected = installer_auto_select_partition(parts, upgrade_mode);

    installer_draw_partition_manager(parts, has_valid_mbr, total_sectors,
                                     selected, upgrade_mode, other_os_count,
                                     chrysalis_count, for_install_flow);
    char c = kbd_getchar();

    if (c == 'r' || c == 'R')
      continue;
    if (c == 'b' || c == 'B' || c == 27)
      return -1;

    if ((c == 'a' || c == 'A') && for_install_flow) {
      int auto_idx = installer_auto_select_partition(parts, upgrade_mode);
      if (auto_idx >= 0 && parts[auto_idx].present) {
        selected = auto_idx;
      }
    } else if ((c == 'c' || c == 'C') && !upgrade_mode) {
      if (!installer_confirm_yes_no(
              "This will overwrite the MBR partition table.",
              "All existing partition entries may be lost. Continue?")) {
        continue;
      }
      if (installer_create_single_fat32_layout(total_sectors) != 0) {
        terminal_printf("\nFailed to create partition layout.\n");
      } else {
        serial("[INSTALLER] Created single FAT32 partition layout.\n");
      }
      continue;
    } else if (c >= '1' && c <= '4') {
      int idx = (int)(c - '1');
      if (!parts[idx].present) {
        terminal_printf("\nPartition slot p%d is empty.\n", idx + 1);
        continue;
      }
      if (parts[idx].type == 0x05 || parts[idx].type == 0x0F ||
          parts[idx].type == 0x85 || parts[idx].type == 0xEE) {
        terminal_printf("\nPartition p%d cannot be used as install target.\n",
                        idx + 1);
        continue;
      }
      selected = idx;
    } else {
      continue;
    }

    if (selected < 0 || selected >= 4 || !parts[selected].present)
      continue;

    if (for_install_flow &&
        parts[selected].count < INSTALLER_MIN_PARTITION_SECTORS) {
      if (!installer_confirm_yes_no(
              "Selected partition is very small (<64 MiB).",
              "Install may fail. Continue anyway?")) {
        continue;
      }
    }

    if (for_install_flow && upgrade_mode &&
        parts[selected].os_hint != INSTALLER_OS_CHRYSALIS) {
      if (!installer_confirm_yes_no(
              "Warning: selected partition does not look like Chrysalis OS.",
              "Upgrade may fail or damage data. Continue anyway?")) {
        continue;
      }
    }

    if (for_install_flow && !upgrade_mode &&
        parts[selected].os_hint != INSTALLER_OS_NONE &&
        parts[selected].os_hint != INSTALLER_OS_CHRYSALIS) {
      if (!installer_confirm_yes_no(
              "Warning: another OS/data was detected on this partition.",
              "Fresh install will format it. Continue?")) {
        continue;
      }
    }

    if (out_index)
      *out_index = selected;
    if (out_lba)
      *out_lba = parts[selected].lba;
    if (out_count)
      *out_count = parts[selected].count;
    if (out_os_hint)
      *out_os_hint = parts[selected].os_hint;
    return 0;
  }
}

static void build_mbr(uint8_t *mbr, const uint8_t *boot_img, int target_index,
                      uint32_t start_lba, uint32_t part_sectors,
                      uint32_t total_sectors) {
  memset(mbr, 0, 512);
  if (boot_img) {
    memcpy(mbr, boot_img, 446); /* boot code only */
  }

  uint8_t cur_mbr[512];
  if (disk_read_sector(0, cur_mbr) == 0 && cur_mbr[510] == 0x55 &&
      cur_mbr[511] == 0xAA) {
    memcpy(mbr + 446, cur_mbr + 446, 64);
  }

  int idx = target_index;
  if (idx < 0 || idx > 3)
    idx = 0;

  uint8_t *p = mbr + 446 + (idx * 16);
  uint32_t count = part_sectors;
  if (count == 0) {
    count = (total_sectors > start_lba) ? (total_sectors - start_lba) : 0;
  }

  for (int i = 0; i < 4; i++) {
    mbr[446 + i * 16] = 0;
  }
  p[0] = 0x80; /* bootable */
  p[4] = 0x0C; /* FAT32 LBA */
  installer_write_u32_le(p + 8, start_lba);
  installer_write_u32_le(p + 12, count);
  mbr[510] = 0x55;
  mbr[511] = 0xAA;
}

static const int UI_SCREEN_W = 80;
static const int UI_SCREEN_H = 25;
static const int UI_BAR_X = 12;
static const int UI_BAR_Y = 13;
static const int UI_BAR_W = 56;
static const int UI_LABEL_X = 10;
static const int UI_VALUE_X = 24;
static const int UI_VALUE_W = 46;
static const int UI_INPUT_X = 24;
static const int UI_INPUT_W = 30;
static int ui_progress_anim_tick = 0;
static int ui_last_percent = -1;

static const uint32_t UI_DELAY_MAJOR_ITERS = 3500000U;
static const uint32_t UI_DELAY_MINOR_ITERS = 700000U;

static const uint8_t UI_BG = 0x17;          /* Blue background, light gray */
static const uint8_t UI_PANEL = 0x1F;       /* Blue background, white */
static const uint8_t UI_PANEL_MUTED = 0x1B; /* Blue background, light cyan */
static const uint8_t UI_PANEL_TITLE = 0x1E; /* Blue background, yellow */
static const uint8_t UI_HEADER = 0x3F;      /* Cyan background, white */
static const uint8_t UI_FOOTER = 0x1E;      /* Blue background, yellow */
static uint8_t ui_input_color = 0x1F;

static int ui_clamp_int(int value, int min_v, int max_v) {
  if (value < min_v)
    return min_v;
  if (value > max_v)
    return max_v;
  return value;
}

static void ui_progress_delay(uint32_t iters) {
  volatile uint32_t spin = 0;
  while (spin < iters) {
    asm volatile("pause");
    spin++;
  }
}

static void ui_fill_rect_textmode(int x, int y, int w, int h, uint8_t color) {
  if (w <= 0 || h <= 0)
    return;

  if (x < 0) {
    w += x;
    x = 0;
  }
  if (y < 0) {
    h += y;
    y = 0;
  }

  if (x >= UI_SCREEN_W || y >= UI_SCREEN_H)
    return;
  if (x + w > UI_SCREEN_W)
    w = UI_SCREEN_W - x;
  if (y + h > UI_SCREEN_H)
    h = UI_SCREEN_H - y;

  if (w <= 0 || h <= 0)
    return;

  for (int yy = 0; yy < h; yy++) {
    for (int xx = 0; xx < w; xx++) {
      terminal_putentryat(' ', color, x + xx, y + yy);
    }
  }
}

static void ui_write_at(int x, int y, uint8_t color, const char *text) {
  if (!text || y < 0 || y >= UI_SCREEN_H || x >= UI_SCREEN_W)
    return;

  size_t i = 0;
  int px = x;
  if (px < 0) {
    i = (size_t)(-px);
    px = 0;
  }

  while (text[i] && px < UI_SCREEN_W) {
    terminal_putentryat(text[i], color, px, y);
    px++;
    i++;
  }
}

static void ui_write_line(int x, int y, int w, uint8_t color,
                          const char *text) {
  ui_fill_rect_textmode(x, y, w, 1, color);
  ui_write_at(x, y, color, text);
}

static void ui_center_text(int y, uint8_t color, const char *text) {
  if (!text)
    return;
  size_t len = strlen(text);
  int x = 0;
  if ((int)len < UI_SCREEN_W)
    x = (UI_SCREEN_W - (int)len) / 2;
  ui_write_at(x, y, color, text);
}

static void ui_draw_panel(int x, int y, int w, int h, uint8_t color,
                          uint8_t shadow_color) {
  ui_fill_rect_textmode(x, y, w, h, color);
  if (shadow_color) {
    if (x + w < UI_SCREEN_W)
      ui_fill_rect_textmode(x + w, y + 1, 1, h, shadow_color);
    if (y + h < UI_SCREEN_H)
      ui_fill_rect_textmode(x + 1, y + h, w, 1, shadow_color);
  }
}

static void ui_u32_to_dec(uint32_t value, char *out, size_t out_sz) {
  if (!out || out_sz == 0)
    return;

  char tmp[16];
  int i = 0;
  if (value == 0) {
    tmp[i++] = '0';
  } else {
    while (value && i < (int)sizeof(tmp)) {
      tmp[i++] = (char)('0' + (value % 10));
      value /= 10;
    }
  }

  int n = i;
  if (n > (int)out_sz - 1)
    n = (int)out_sz - 1;

  for (int j = 0; j < n; j++) {
    out[j] = tmp[i - 1 - j];
  }
  out[n] = 0;
}

static void ui_append_str(char *out, size_t out_sz, const char *src) {
  if (!out || out_sz == 0 || !src)
    return;
  size_t len = strlen(out);
  if (len >= out_sz - 1)
    return;
  while (*src && len + 1 < out_sz) {
    out[len++] = *src++;
  }
  out[len] = 0;
}

static void ui_draw_header(const char *title) {
  ui_fill_rect_textmode(0, 0, UI_SCREEN_W, 1, UI_HEADER);
  if (title)
    ui_center_text(0, UI_HEADER, title);
}

static void ui_draw_footer(const char *text) {
  ui_fill_rect_textmode(0, UI_SCREEN_H - 1, UI_SCREEN_W, 1, UI_FOOTER);
  if (text)
    ui_center_text(UI_SCREEN_H - 1, UI_FOOTER, text);
}

static void ui_draw_welcome_screen(bool scan_ok, int present_count,
                                   int chrysalis_count, int other_os_count) {
  terminal_set_color(UI_BG);
  terminal_clear();
  ui_draw_header("ChrysalisOS Setup");
  ui_fill_rect_textmode(0, 1, UI_SCREEN_W, UI_SCREEN_H - 2, UI_BG);
  ui_draw_panel(6, 3, 68, 16, UI_PANEL, 0x10);

  ui_write_at(10, 5, UI_PANEL_TITLE, "Welcome");
  ui_write_at(10, 6, UI_PANEL_MUTED,
              "This will install or upgrade ChrysalisOS.");

  char part_line[80];
  part_line[0] = 0;
  if (!scan_ok) {
    ui_append_str(part_line, sizeof(part_line),
                  "Detected partitions: scan failed (disk not ready)");
  } else {
    char num[12];
    ui_append_str(part_line, sizeof(part_line), "Detected partitions: ");
    ui_u32_to_dec((uint32_t)present_count, num, sizeof(num));
    ui_append_str(part_line, sizeof(part_line), num);
    ui_append_str(part_line, sizeof(part_line), " (Chrysalis: ");
    ui_u32_to_dec((uint32_t)chrysalis_count, num, sizeof(num));
    ui_append_str(part_line, sizeof(part_line), num);
    ui_append_str(part_line, sizeof(part_line), ", Other OS: ");
    ui_u32_to_dec((uint32_t)other_os_count, num, sizeof(num));
    ui_append_str(part_line, sizeof(part_line), num);
    ui_append_str(part_line, sizeof(part_line), ")");
  }
  ui_write_at(10, 8, UI_PANEL_MUTED, part_line);

  ui_write_at(10, 10, UI_PANEL_TITLE, "Choose an option");
  ui_write_at(12, 12, UI_PANEL,
              "1  Fresh Install   - Wipes disk, installs new system");
  ui_write_at(12, 13, UI_PANEL,
              "2  Upgrade         - Keeps files and updates system");
  ui_write_at(12, 14, UI_PANEL,
              "P  Partition Mgr   - Detect other OS, choose target");
  ui_write_at(12, 15, UI_PANEL,
              "J  Recovery Shell  - Opens a command shell");
  ui_write_at(12, 16, UI_PANEL,
              "0  Shutdown        - Power off");

  ui_draw_footer("Press 1/2/P/J/0 to continue");
}

static void ui_draw_user_setup_screen(void) {
  terminal_set_color(UI_BG);
  terminal_clear();
  ui_draw_header("ChrysalisOS User Setup");
  ui_fill_rect_textmode(0, 1, UI_SCREEN_W, UI_SCREEN_H - 2, UI_BG);
  ui_draw_panel(6, 3, 68, 16, UI_PANEL, 0x10);
  ui_write_at(10, 5, UI_PANEL_TITLE, "Create your account");
  ui_write_at(10, 8, UI_PANEL, "Username:");
  ui_write_at(10, 10, UI_PANEL, "Password:");
  ui_write_at(10, 12, UI_PANEL, "Device Name:");
  ui_fill_rect_textmode(UI_INPUT_X, 8, UI_INPUT_W, 1, UI_PANEL_MUTED);
  ui_fill_rect_textmode(UI_INPUT_X, 10, UI_INPUT_W, 1, UI_PANEL_MUTED);
  ui_fill_rect_textmode(UI_INPUT_X, 12, UI_INPUT_W, 1, UI_PANEL_MUTED);
  ui_draw_footer("Press Enter after each field");
}

static void ui_make_progress_text(int percent, char *out, size_t out_sz) {
  if (!out || out_sz == 0)
    return;

  char num[8];
  ui_u32_to_dec((uint32_t)percent, num, sizeof(num));

  size_t p = 0;
  const char *prefix = "Progress: ";
  while (*prefix && p + 1 < out_sz) {
    out[p++] = *prefix++;
  }
  for (size_t i = 0; num[i] && p + 1 < out_sz; i++) {
    out[p++] = num[i];
  }
  if (p + 1 < out_sz)
    out[p++] = '%';
  out[p] = 0;
}

static void ui_draw_progress_frame(bool upgrade_mode) {
  terminal_set_color(UI_BG);
  terminal_clear();

  if (upgrade_mode) {
    ui_draw_header("ChrysalisOS Upgrade");
  } else {
    ui_draw_header("ChrysalisOS Installation");
  }

  terminal_set_color(UI_BG);
  ui_fill_rect_textmode(0, 1, UI_SCREEN_W, UI_SCREEN_H - 2, UI_BG);
  ui_draw_panel(6, 3, 68, 16, UI_PANEL, 0x10);

  ui_write_at(UI_LABEL_X, 5, UI_PANEL_TITLE, "Installing system components");
  ui_write_at(UI_LABEL_X, 6, UI_PANEL_MUTED, "Do not power off your computer.");
  ui_write_at(UI_LABEL_X, 8, UI_PANEL, "Stage:");
  ui_write_at(UI_LABEL_X, 9, UI_PANEL, "Detail:");
  ui_write_at(UI_LABEL_X, 11, UI_PANEL, "Progress: 0%");

  ui_fill_rect_textmode(UI_BAR_X - 2, UI_BAR_Y - 1, UI_BAR_W + 4, 3, UI_BG);
  terminal_putentryat('[', UI_PANEL_MUTED, UI_BAR_X - 1, UI_BAR_Y);
  for (int i = 0; i < UI_BAR_W; i++) {
    terminal_putentryat('.', UI_PANEL_MUTED, UI_BAR_X + i, UI_BAR_Y);
  }
  terminal_putentryat(']', UI_PANEL_MUTED, UI_BAR_X + UI_BAR_W, UI_BAR_Y);
  ui_write_at(UI_BAR_X - 2, UI_BAR_Y + 1, UI_PANEL_MUTED, "0%");
  ui_write_at(UI_BAR_X + UI_BAR_W - 3, UI_BAR_Y + 1, UI_PANEL_MUTED, "100%");

  ui_draw_footer("Installing... please wait");
  ui_progress_anim_tick = 0;
}

static void ui_draw_success_screen(bool upgrade_mode) {
  terminal_set_color(UI_BG);
  terminal_clear();
  ui_draw_header(upgrade_mode ? "ChrysalisOS Upgrade Complete"
                              : "ChrysalisOS Installation Complete");
  ui_fill_rect_textmode(0, 1, UI_SCREEN_W, UI_SCREEN_H - 2, UI_BG);
  ui_draw_panel(6, 3, 68, 18, UI_PANEL, 0x10);

  ui_write_at(10, 5, UI_PANEL_TITLE,
              upgrade_mode ? "Upgrade finished" : "Installation finished");
  ui_write_at(10, 6, UI_PANEL_MUTED,
              "ChrysalisOS is ready to boot.");

  char line[80];
  line[0] = 0;
#ifdef CHRYVER
  ui_append_str(line, sizeof(line), "Version: ");
  ui_append_str(line, sizeof(line), CHRYVER);
#else
  ui_append_str(line, sizeof(line), "Version: unknown");
#endif
  ui_write_at(10, 8, UI_PANEL, line);
  ui_write_at(10, 9, UI_PANEL, "Website: chrysalisos.netlify.app");

  ui_write_at(10, 11, UI_PANEL_TITLE, "Choose what to do next");
  ui_write_at(12, 13, UI_PANEL, "M  Shutdown  - Power off the computer");
  ui_write_at(12, 14, UI_PANEL, "R  Reboot    - Boot into ChrysalisOS");
  ui_write_at(12, 15, UI_PANEL, "J  Recovery  - Open recovery shell");

  ui_draw_footer("Press M / R / J");
}

static void ui_progress_update(int percent, const char *stage,
                               const char *detail) {
  int pct = ui_clamp_int(percent, 0, 100);
  int changed_percent = (pct != ui_last_percent);
  int filled = (pct * UI_BAR_W) / 100;

  ui_fill_rect_textmode(UI_VALUE_X, 8, UI_VALUE_W, 1, UI_PANEL);
  ui_fill_rect_textmode(UI_VALUE_X, 9, UI_VALUE_W, 1, UI_PANEL);
  ui_write_at(UI_VALUE_X, 8, UI_PANEL, stage ? stage : "");
  ui_write_at(UI_VALUE_X, 9, UI_PANEL_MUTED, detail ? detail : "");

  char progress_text[32];
  ui_make_progress_text(pct, progress_text, sizeof(progress_text));
  ui_write_line(UI_LABEL_X, 11, 60, UI_PANEL, progress_text);

  terminal_putentryat('[', UI_PANEL_MUTED, UI_BAR_X - 1, UI_BAR_Y);
  terminal_putentryat(']', UI_PANEL_MUTED, UI_BAR_X + UI_BAR_W, UI_BAR_Y);
  for (int i = 0; i < UI_BAR_W; i++) {
    if (i < filled) {
      terminal_putentryat('=', 0x3F, UI_BAR_X + i, UI_BAR_Y);
    } else {
      terminal_putentryat('.', UI_PANEL_MUTED, UI_BAR_X + i, UI_BAR_Y);
    }
  }

  if (filled > 0) {
    int hi = ui_progress_anim_tick % filled;
    terminal_putentryat('>', UI_PANEL_TITLE, UI_BAR_X + hi, UI_BAR_Y);
  }
  ui_progress_anim_tick++;
  ui_last_percent = pct;

  if (changed_percent) {
    ui_progress_delay(UI_DELAY_MAJOR_ITERS);
  } else {
    ui_progress_delay(UI_DELAY_MINOR_ITERS);
  }
}

static int installer_count_modules(uint32_t addr) {
  int module_count = 0;
  struct multiboot2_tag *tag = (struct multiboot2_tag *)(uintptr_t)(addr + 8);
  while (tag->type != MULTIBOOT2_TAG_TYPE_END) {
    if (tag->type == MULTIBOOT2_TAG_TYPE_MODULE)
      module_count++;
    tag = (struct multiboot2_tag *)((uint8_t *)tag + ((tag->size + 7) & ~7));
  }
  return module_count;
}

/* Keyboard polling for menu (USB + PS/2) */
static int usb_try_getchar(char *out) {
  input_event_t ev;
  usb_poll();
  while (input_pop(&ev)) {
    if (ev.type == INPUT_KEYBOARD && ev.pressed) {
      *out = (char)ev.keycode;
      return 1;
    }
  }
  return 0;
}

static int ps2_try_getchar(char *out) {
  if (!(inb(0x64) & 1))
    return 0;
  uint8_t scancode = inb(0x60);
  if (scancode & 0x80)
    return 0; // ignore release
  static const char map[] = {
      0,   27,  '1',  '2',  '3',  '4', '5', '6',  '7', '8', '9', '0',
      '-', '=', '\b', '\t', 'q',  'w', 'e', 'r',  't', 'y', 'u', 'i',
      'o', 'p', '[',  ']',  '\n', 0,   'a', 's',  'd', 'f', 'g', 'h',
      'j', 'k', 'l',  ';',  '\'', '`', 0,   '\\', 'z', 'x', 'c', 'v',
      'b', 'n', 'm',  ',',  '.',  '/', 0,   '*',  0,   ' '};
  if (scancode < sizeof(map)) {
    *out = map[scancode];
    return (*out != 0);
  }
  return 0;
}

static char kbd_getchar() {
  char c = 0;
  while (1) {
    if (usb_try_getchar(&c))
      return c;
    if (ps2_try_getchar(&c))
      return c;
  }
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
        terminal_putentryat(' ', ui_input_color, terminal_get_cursor_x() - 1,
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
        terminal_putentryat(' ', ui_input_color, terminal_get_cursor_x() - 1,
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
  ui_input_color = 0x07;

  // Try to mount FAT32 if not already done
  serial("[RECOVERY] Attempting to mount filesystem...\n");
  ata_init();
  uint32_t start_lba = 2048;
  if (fat32_init(0, start_lba) != 0) {
    if (fat32_init(0, 0) != 0) {
      serial("[RECOVERY] Failed to mount FAT32\n");
      terminal_printf("WARN: Filesystem not mounted.\n");
    } else {
      fat32_set_mounted(0, 'a');
      terminal_printf("Filesystem mounted at LBA 0\n");
    }
  } else {
    fat32_set_mounted(start_lba, 'a');
    terminal_printf("Filesystem mounted at LBA %d\n", start_lba);
  }

  terminal_set_color(0x07); // Light Grey on Black
  terminal_printf("Chrysalis OS Recovery Shell\n");
  terminal_printf(
      "Commands: ls, cd, cat, reboot, shutdown, disk, free, exit\n\n");

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
      } else if (strcmp(argv[0], "cat") == 0) {
        // Simple cat implementation for recovery
        if (argc < 2) {
          terminal_printf("Usage: cat <file>\n");
        } else {
          int sz = fat32_get_file_size(argv[1]);
          if (sz < 0) {
            terminal_printf("File not found: %s\n", argv[1]);
          } else {
            if (sz > 16384)
              sz = 16384; // Limit size
            char *fbuf = (char *)kmalloc(sz + 1);
            if (fbuf) {
              fat32_read_file(argv[1], fbuf, sz);
              fbuf[sz] = 0;
              terminal_printf("%s\n", fbuf);
              kfree(fbuf);
            } else {
              terminal_printf("OOM\n");
            }
          }
        }
      } else if (strcmp(argv[0], "disk") == 0) {
        uint32_t cap = disk_get_capacity();
        terminal_printf("Disk Capacity: %u sectors (%u MB)\n", cap,
                        (cap * 512) / 1024 / 1024);
      } else if (strcmp(argv[0], "free") == 0) {
        // crude memory check since we don't have pmm here fully exported
        terminal_printf(
            "Installer Memory: kmalloc_simple used unknown bytes.\n");
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
        terminal_printf("  cat [file]      - Read file\n");
        terminal_printf("  disk            - Show disk info\n");
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

  /* Initialize input + USB (for bare-metal USB keyboards) */
  input_init();
  usb_core_init();

  /* 0. Welcome Menu */
  installer_partition_info_t detected_parts[4];
  bool has_valid_mbr = false;
  int present_count = 0;
  int other_os_count = 0;
  int chrysalis_count = 0;
  bool scan_ok =
      (installer_scan_partitions(detected_parts, &has_valid_mbr, &present_count,
                                 &other_os_count, &chrysalis_count) == 0);
  ui_draw_welcome_screen(scan_ok, present_count, chrysalis_count,
                         other_os_count);

  char choice = 0;
  while (choice != '1' && choice != '2' && choice != '0' && choice != 'J' &&
         choice != 'j' && choice != 'p' && choice != 'P') {
    choice = kbd_getchar();
  }

  if (choice == '0') {
    serial("[INSTALLER] Shutting down...\n");
    cmd_shutdown(NULL);
    return;
  }

  if (choice == 'J' || choice == 'j') {
    recovery_shell();
    installer_main(magic, addr);
    return;
  }

  if (choice == 'P' || choice == 'p') {
    int idx = g_install_target_part_index;
    uint32_t lba = g_install_target_lba;
    uint32_t cnt = g_install_target_count;
    int hint = g_install_target_os_hint;
    if (installer_partition_manager_select(false, false, &idx, &lba, &cnt,
                                           &hint) == 0) {
      g_install_target_part_index = idx;
      g_install_target_lba = lba;
      g_install_target_count = cnt;
      g_install_target_os_hint = hint;
      serial("[INSTALLER] Manual target set: p%d lba=%u sectors=%u os=%s\n",
             idx + 1, lba, cnt, installer_os_hint_name(hint));
    }
    installer_main(magic, addr);
    return;
  }
  serial("> Choice: %c selected.\n\n", choice);

  bool upgrade_mode = (choice == '2');

  int target_part_index = g_install_target_part_index;
  uint32_t start_lba = g_install_target_lba;
  uint32_t target_part_sectors = g_install_target_count;
  int target_os_hint = g_install_target_os_hint;
  if (installer_partition_manager_select(upgrade_mode, true, &target_part_index,
                                         &start_lba, &target_part_sectors,
                                         &target_os_hint) != 0) {
    installer_main(magic, addr);
    return;
  }
  g_install_target_part_index = target_part_index;
  g_install_target_lba = start_lba;
  g_install_target_count = target_part_sectors;
  g_install_target_os_hint = target_os_hint;

  serial("[INSTALLER] Selected target p%d: lba=%u sectors=%u os=%s\n",
         target_part_index + 1, start_lba, target_part_sectors,
         installer_os_hint_name(target_os_hint));

  serial_set_vga_mirror(0);
  ui_draw_progress_frame(upgrade_mode);
  ui_progress_update(0, "Preparing installer", "Initializing subsystems...");

  /* 1. Initialize Subsystems */
  kmalloc_init();
  ui_progress_update(2, "Initializing hardware",
                     "Bringing up ATA controller...");

  serial("[INSTALLER] Initializing ATA...\n");
  ata_init();
  /* Keep cache flush enabled for verified writes */
  ata_set_skip_cache_flush(0);
  ui_progress_update(6, "Initializing hardware", "Target disk ready.");

  /* 2. Format / Prepare Target Disk */
  uint32_t total_sectors = disk_get_capacity();
  if (total_sectors == 0)
    total_sectors = 262144; // Default 128MB
  if (target_part_sectors == 0)
    target_part_sectors =
        (total_sectors > start_lba) ? (total_sectors - start_lba) : 0;

  if (!upgrade_mode) {
    ui_progress_update(8, "Preparing target disk",
                       "Formatting partition and filesystem...");
    serial("[INSTALLER] Action: Formatting p%d (LBA %u, sectors=%u)...\n",
           target_part_index + 1, start_lba, target_part_sectors);
    if (fat32_format(start_lba, target_part_sectors, "CHRYSALIS") != 0) {
      serial("[INSTALLER] ERROR: Formatting failed!\n");
      return;
    }
    serial("[INSTALLER] Format Complete.\n");
  } else {
    ui_progress_update(8, "Preparing target disk",
                       "Verifying existing filesystem for upgrade...");
    serial("[INSTALLER] Action: UPGRADE (Verifying existing Filesystem at LBA "
           "%u, p%d)...\n",
           start_lba, target_part_index + 1);
  }
  ui_progress_update(20, "Preparing target disk", "Disk phase complete.");

  /* 2.1 User Setup (Only for Fresh Install) */
  char username[32];
  char password[32];
  char hostname[32];

  if (!upgrade_mode) {
    ui_progress_update(20, "User setup", "Collecting account information...");
    ui_draw_user_setup_screen();
    ui_input_color = UI_PANEL_MUTED;
    terminal_set_cursor_pos(UI_INPUT_X, 8);
    get_input(username, 32);
    terminal_set_cursor_pos(UI_INPUT_X, 10);
    get_password(password, 32);
    terminal_set_cursor_pos(UI_INPUT_X, 12);
    get_input(hostname, 32);

    serial("[INSTALLER] User setup: user='%s' host='%s'\n", username, hostname);
    ui_draw_progress_frame(upgrade_mode);
    ui_progress_update(30, "User setup", "Account data captured.");
  } else {
    serial("[INSTALLER] Skipping user setup (upgrade mode - preserving "
           "existing users)\n");
    ui_progress_update(30, "User setup", "Skipped in upgrade mode.");
  }

  ui_progress_update(31, "Mounting filesystem",
                     "Mounting target volume and running checks...");
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
  ui_progress_update(38, "Mounting filesystem", "Filesystem checks complete.");

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
  ui_progress_update(39, "Creating directory tree",
                     "Creating /boot, /system and application folders...");
  serial("[INSTALLER] Creating directories...\n");
  char boot_path[6] = {'/', 'b', 'o', 'o', 't', 0};
  char chrys_path[16] = {'/', 'b', 'o', 'o', 't', '/', 'c', 'h',
                         'r', 'y', 's', 'a', 'l', 'i', 's', 0};
  char grub_path[11] = {'/', 'b', 'o', 'o', 't', '/', 'g', 'r', 'u', 'b', 0};
  char system_path[8] = {'/', 's', 'y', 's', 't', 'e', 'm', 0};
  char icons_dir[14] = {'/', 's', 'y', 's', 't', 'e', 'm',
                        '/', 'i', 'c', 'o', 'n', 's', 0};
  char services_dir[18] = {'/', 's', 'y', 's', 't', 'e', 'm',
                           '/', 's', 'e', 'r', 'v', 'i', 'c', 'e', 's', 0};
  char themes_dir[18] = {'/', 'b', 'o', 'o', 't', '/', 'g', 'r', 'u',
                         'b', '/', 't', 'h', 'e', 'm', 'e', 's', 0};
  char theme_dir[28] = {'/', 'b', 'o', 'o', 't', '/', 'g', 'r', 'u', 'b',
                        '/', 't', 'h', 'e', 'm', 'e', 's', '/', 'c', 'h',
                        'r', 'y', 's', 'a', 'l', 'i', 's', 0};
  char apps_dir[13] = {'/', 's', 'y', 's', 't', 'e', 'm',
                       '/', 'a', 'p', 'p', 's', 0};

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
  mr = fat32_create_directory_verified(services_dir, 1);
  if (mr != 0 && !upgrade_mode) {
    serial("[INSTALLER] WARN: mkdir /system/services failed (err=%d)\n", mr);
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
  mr = fat32_create_directory_verified(apps_dir, 1);
  if (mr != 0 && !upgrade_mode) {
    serial("[INSTALLER] WARN: mkdir /system/apps failed (err=%d)\n", mr);
  }
  ui_progress_update(48, "Creating directory tree",
                     "Directory phase complete.");

  /* Directory listings disabled to reduce stack usage and avoid instability */

  /* 4. Locate Source Files (Multiboot Modules) */
  void *kernel_data = NULL;
  size_t kernel_size = 0;
  void *boot_img = NULL;
  size_t boot_img_size = 0;
  void *core_img = NULL;
  size_t core_img_size = 0;
  void *theme_txt_data = NULL;
  size_t theme_txt_size = 0;
  void *bg_tga_data = NULL;
  size_t bg_tga_size = 0;
  void *sel_tga_data = NULL;
  size_t sel_tga_size = 0;
  void *bg_bmp_data = NULL;
  size_t bg_bmp_size = 0;
  void *kernel64_data = NULL;
  size_t kernel64_size = 0;

  int module_total = installer_count_modules(addr);
  if (module_total <= 0)
    module_total = 1;
  int module_done = 0;
  char module_total_num[12];
  char module_total_msg[64];
  ui_u32_to_dec((uint32_t)module_total, module_total_num,
                sizeof(module_total_num));
  size_t mt = 0;
  const char *mt_prefix = "Modules to process: ";
  while (*mt_prefix && mt + 1 < sizeof(module_total_msg))
    module_total_msg[mt++] = *mt_prefix++;
  for (size_t i = 0; module_total_num[i] && mt + 1 < sizeof(module_total_msg);
       i++) {
    module_total_msg[mt++] = module_total_num[i];
  }
  module_total_msg[mt] = 0;
  ui_progress_update(48, "Scanning installer modules", module_total_msg);

  /* Scan multidoob tags (parsed manually here as we need raw addresses) */
  struct multiboot2_tag *tag = (struct multiboot2_tag *)(uintptr_t)(addr + 8);
  while (tag->type != MULTIBOOT2_TAG_TYPE_END) {
    if (tag->type == MULTIBOOT2_TAG_TYPE_MODULE) {
      struct multiboot2_tag_module *mod = (struct multiboot2_tag_module *)tag;
      const char *cmdline = mod->string; /* Correct field name: char string[] */
      char current_mod_name[64];
      current_mod_name[0] = 0;

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
        normalize_module_name_len(cmdline, cmd_len, current_mod_name,
                                  sizeof(current_mod_name));
        serial("[INSTALLER] Module name parsed: '%s'\n", current_mod_name);

        int m_kernel = strcmp(current_mod_name, "kernel.bin");
        int m_kernel64 = strcmp(current_mod_name, "kernel64.bin");
        int m_boot = strcmp(current_mod_name, "boot.img");
        int m_core = strcmp(current_mod_name, "core.img");
        serial("[INSTALLER] mod_name cmp: kernel=%d boot=%d core=%d\n",
               m_kernel, m_boot, m_core);

        if (m_kernel == 0) {
          kernel_data = (void *)(uintptr_t)mod->mod_start;
          kernel_size = mod->mod_end - mod->mod_start;
          serial("[INSTALLER] Assigned to kernel.bin\n");
        } else if (m_kernel64 == 0) {
          kernel64_data = (void *)(uintptr_t)mod->mod_start;
          kernel64_size = mod->mod_end - mod->mod_start;
          serial("[INSTALLER] Assigned to kernel64.bin\n");
        } else if (m_boot == 0) {
          boot_img = (void *)(uintptr_t)mod->mod_start;
          boot_img_size = mod->mod_end - mod->mod_start;
          serial("[INSTALLER] Assigned to boot.img\n");
        } else if (m_core == 0) {
          core_img = (void *)(uintptr_t)mod->mod_start;
          core_img_size = mod->mod_end - mod->mod_start;
          serial("[INSTALLER] Assigned to core.img\n");
        } else if (strcmp(current_mod_name, "theme.txt") == 0) {
          theme_txt_data = (void *)(uintptr_t)mod->mod_start;
          theme_txt_size = mod->mod_end - mod->mod_start;
          serial("[INSTALLER] Assigned to theme.txt\n");
        } else if (strcmp(current_mod_name, "background.tga") == 0) {
          bg_tga_data = (void *)(uintptr_t)mod->mod_start;
          bg_tga_size = mod->mod_end - mod->mod_start;
          serial("[INSTALLER] Assigned to background.tga\n");
        } else if (strcmp(current_mod_name, "select_c.tga") == 0) {
          sel_tga_data = (void *)(uintptr_t)mod->mod_start;
          sel_tga_size = mod->mod_end - mod->mod_start;
          serial("[INSTALLER] Assigned to select_c.tga\n");
        } else if (strcmp(current_mod_name, "bg.bmp") == 0) {
          bg_bmp_data = (void *)(uintptr_t)mod->mod_start;
          bg_bmp_size = mod->mod_end - mod->mod_start;
          serial("[INSTALLER] Assigned to bg.bmp (desktop wallpaper)\n");
        } else if (has_extension(current_mod_name, ".bmp")) {
          char path[64] = "/system/icons/";
          size_t off = strlen(path);
          memcpy(path + off, current_mod_name, strlen(current_mod_name) + 1);
          serial("[INSTALLER] Dynamic Icon: %s -> %s\n", current_mod_name,
                 path);
          fat32_create_file_verified(path, (void *)(uintptr_t)mod->mod_start,
                                     (uint32_t)(mod->mod_end - mod->mod_start),
                                     0);
          kmalloc_reset();
        } else if (has_extension(current_mod_name, ".srv")) {
          char path[64] = "/system/services/";
          size_t off = strlen(path);
          memcpy(path + off, current_mod_name, strlen(current_mod_name) + 1);
          serial("[INSTALLER] Service file: %s -> %s\n", current_mod_name, path);
          fat32_create_file_verified(path, (void *)(uintptr_t)mod->mod_start,
                                     (uint32_t)(mod->mod_end - mod->mod_start),
                                     0);
          kmalloc_reset();
        } else if (has_extension(current_mod_name, ".petal")) {
          char path[64] = "/system/apps/";
          size_t off = strlen(path);
          memcpy(path + off, current_mod_name, strlen(current_mod_name) + 1);
          serial("[INSTALLER] Dynamic App: %s -> %s\n", current_mod_name, path);
          fat32_create_file_verified(path, (void *)(uintptr_t)mod->mod_start,
                                     (uint32_t)(mod->mod_end - mod->mod_start),
                                     0);
          kmalloc_reset();
        } else {
          serial("[INSTALLER] Module '%s' did not match any expected file.\n",
                 cmdline);
        }
      }

      module_done++;
      int module_pct = 48 + (module_done * 20) / module_total;
      if (module_pct > 68)
        module_pct = 68;

      char module_detail[72];
      size_t md = 0;
      const char *md_prefix = "Processing module: ";
      while (*md_prefix && md + 1 < sizeof(module_detail))
        module_detail[md++] = *md_prefix++;
      if (current_mod_name[0]) {
        for (size_t i = 0;
             current_mod_name[i] && md + 1 < sizeof(module_detail); i++) {
          module_detail[md++] = current_mod_name[i];
        }
      } else {
        const char *unknown = "(unnamed)";
        for (size_t i = 0; unknown[i] && md + 1 < sizeof(module_detail); i++) {
          module_detail[md++] = unknown[i];
        }
      }
      module_detail[md] = 0;
      ui_progress_update(module_pct, "Scanning installer modules",
                         module_detail);
    }
    tag = (struct multiboot2_tag *)((uint8_t *)tag + ((tag->size + 7) & ~7));
  }
  ui_progress_update(68, "Scanning installer modules",
                     "Module scan and dynamic installs complete.");

  /* 4.1 Install GRUB boot code + core.img */
  ui_progress_update(69, "Installing bootloader",
                     "Preparing GRUB boot records...");
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
  build_mbr(mbr, (const uint8_t *)boot_img, target_part_index, start_lba,
            target_part_sectors, total_sectors);
  ata_set_allow_mbr_write(1);
  int mwr = disk_write_sector(0, mbr);
  ata_set_allow_mbr_write(0);
  if (mwr != 0) {
    serial("[INSTALLER] ERROR: failed to write MBR sector\n");
    kfree(mbr);
    return;
  }
  kfree(mbr);

  serial("[INSTALLER] Writing GRUB core.img (%d bytes)...\n",
         (int)core_img_size);
  write_sectors(1, core_img, (uint32_t)core_img_size);
  ui_progress_update(72, "Installing bootloader",
                     "Writing GRUB configuration...");

  /* 5. Write GRUB config */
  char grub_cfg[4096];
  grub_cfg[0] = 0;
  ui_append_str(grub_cfg, sizeof(grub_cfg), "# Load graphical modules\n");
  ui_append_str(grub_cfg, sizeof(grub_cfg), "insmod png\n");
  ui_append_str(grub_cfg, sizeof(grub_cfg), "insmod tga\n");
  ui_append_str(grub_cfg, sizeof(grub_cfg), "insmod jpeg\n");
  ui_append_str(grub_cfg, sizeof(grub_cfg), "insmod font\n");
  ui_append_str(grub_cfg, sizeof(grub_cfg), "insmod gfxterm\n");
  ui_append_str(grub_cfg, sizeof(grub_cfg), "insmod vbe\n");
  ui_append_str(grub_cfg, sizeof(grub_cfg), "insmod vga\n");
  ui_append_str(grub_cfg, sizeof(grub_cfg), "insmod all_video\n\n");
  ui_append_str(grub_cfg, sizeof(grub_cfg), "# Set graphics mode\n");
  ui_append_str(grub_cfg, sizeof(grub_cfg),
                "set gfxmode=1024x768,800x600,auto\n");
  ui_append_str(grub_cfg, sizeof(grub_cfg), "set gfxpayload=keep\n\n");
  ui_append_str(grub_cfg, sizeof(grub_cfg),
                "# Enable graphical terminal\n");
  ui_append_str(grub_cfg, sizeof(grub_cfg), "terminal_output gfxterm\n\n");
  ui_append_str(grub_cfg, sizeof(grub_cfg), "# Set theme\n");
  ui_append_str(grub_cfg, sizeof(grub_cfg),
                "set theme=/boot/grub/themes/chrysalis/theme.txt\n\n");
  ui_append_str(grub_cfg, sizeof(grub_cfg), "set timeout=5\n");
  ui_append_str(grub_cfg, sizeof(grub_cfg), "set default=0\n\n");

  ui_append_str(grub_cfg, sizeof(grub_cfg),
                "menuentry \"Chrysalis OS (Console)\" {\n");
  ui_append_str(grub_cfg, sizeof(grub_cfg),
                "  multiboot2 /boot/chrysalis/kernel.bin\n");
  ui_append_str(grub_cfg, sizeof(grub_cfg), "  boot\n}\n\n");

  if (kernel64_data && kernel64_size > 0) {
    ui_append_str(grub_cfg, sizeof(grub_cfg),
                  "menuentry \"Chrysalis OS (64-bit Prototype)\" {\n");
    ui_append_str(grub_cfg, sizeof(grub_cfg),
                  "  set gfxpayload=text\n");
    ui_append_str(grub_cfg, sizeof(grub_cfg),
                  "  terminal_output console\n");
    ui_append_str(grub_cfg, sizeof(grub_cfg),
                  "  multiboot2 /boot/chrysalis/kernel64.bin\n");
    ui_append_str(grub_cfg, sizeof(grub_cfg), "  boot\n}\n\n");

    ui_append_str(grub_cfg, sizeof(grub_cfg),
                  "menuentry \"Chrysalis OS (64-bit Prototype, Linux ABI)\" {\n");
    ui_append_str(grub_cfg, sizeof(grub_cfg),
                  "  set gfxpayload=text\n");
    ui_append_str(grub_cfg, sizeof(grub_cfg),
                  "  terminal_output console\n");
    ui_append_str(grub_cfg, sizeof(grub_cfg),
                  "  multiboot2 /boot/chrysalis/kernel64.bin linuxabi=1\n");
    ui_append_str(grub_cfg, sizeof(grub_cfg), "  boot\n}\n\n");
  }

  ui_append_str(grub_cfg, sizeof(grub_cfg),
                "menuentry \"Chrysalis OS (Console, PIC Safe)\" {\n");
  ui_append_str(grub_cfg, sizeof(grub_cfg),
                "  multiboot2 /boot/chrysalis/kernel.bin apic=off\n");
  ui_append_str(grub_cfg, sizeof(grub_cfg), "  boot\n}\n\n");

  ui_append_str(grub_cfg, sizeof(grub_cfg),
                "menuentry \"Chrysalis OS (Console, Debug)\" {\n");
  ui_append_str(grub_cfg, sizeof(grub_cfg),
                "  multiboot2 /boot/chrysalis/kernel.bin --debug\n");
  ui_append_str(grub_cfg, sizeof(grub_cfg), "  boot\n}\n");
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
  ui_progress_update(78, "Installing bootloader",
                     "GRUB and theme assets installed.");

  /* 4.2 Install desktop wallpaper */
  if (bg_bmp_data && bg_bmp_size > 0) {
    serial("[INSTALLER] Installing desktop wallpaper /system/bg.bmp (%d bytes)...\n",
           (int)bg_bmp_size);
    fat32_create_file_verified("/system/bg.bmp", bg_bmp_data,
                               (uint32_t)bg_bmp_size, 1);
    serial("[INSTALLER] bg.bmp installed OK.\n");
  } else {
    serial("[INSTALLER] WARN: bg.bmp module not found, desktop will use solid color.\n");
  }

  /* 6. Install Kernel (chunked) */
  if (kernel_data && kernel_size > 0) {
    ui_progress_update(79, "Installing kernel",
                       "Writing /boot/chrysalis/kernel.bin...");
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
      int kernel_pct = 78 + (int)((offset * 17U) / kernel_size);
      if (kernel_pct > 95)
        kernel_pct = 95;
      ui_progress_update(kernel_pct, "Installing kernel",
                         "Writing kernel chunks to disk...");
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

  if (kernel64_data && kernel64_size > 0) {
    ui_progress_update(95, "Installing kernel",
                       "Writing /boot/chrysalis/kernel64.bin...");
    serial("[INSTALLER] Installing kernel64.bin (%d bytes)...\n",
           (int)kernel64_size);
    int r64 = fat32_create_file_verified("/boot/chrysalis/kernel64.bin",
                                         kernel64_data,
                                         (uint32_t)kernel64_size, 1);
    if (r64 != 0) {
      serial("[INSTALLER] WARN: Failed to write kernel64.bin (err=%d)\n", r64);
    }
  }

  /* Files (.bmp and .petal) are now installed dynamically during the module
   * scan. */

  /* 7.1 Create User Data (Only for Fresh Install) */
  ui_progress_update(95, "Finalizing installation",
                     "Writing user data and verification...");
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

  ui_progress_update(100, "Finalizing installation",
                     "Installation complete. Opening success screen...");
  serial("\n[INSTALLER] Installation Complete.\n");

  /* 8. Success Screen */
  while (true) {
    ui_draw_success_screen(upgrade_mode);

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
      } else if (fc == 'j' || fc == 'J') {
        recovery_shell();
        back_to_menu = true;
      }
    }
  }
}
