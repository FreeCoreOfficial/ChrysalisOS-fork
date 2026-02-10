/* kernel/cmds/fat.cpp */
#include "fat.h"
#include "../fs/fat/fat.h"
#include "../mem/kmalloc.h"
#include "../string.h"
#include "../terminal.h"
#include "disk.h" // Acces la g_assigns

extern void terminal_printf(const char *fmt, ...);
extern "C" void serial(const char *fmt, ...);

static bool is_fat_initialized = false;
static uint32_t current_lba = 0;
static char current_letter = 0;

extern "C" void fat32_set_mounted(uint32_t lba, char letter) {
  is_fat_initialized = true;
  current_lba = lba;
  current_letter = letter;
}

/* --- FAT32 Structures & Helpers (Local Implementation) --- */

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

struct fat_dir_entry {
  char name[11];
  uint8_t attr;
  uint8_t reserved;
  uint8_t ctime_tenth;
  uint16_t ctime;
  uint16_t cdate;
  uint16_t adate;
  uint16_t cluster_hi;
  uint16_t mtime;
  uint16_t mdate;
  uint16_t cluster_low;
  uint32_t size;
} __attribute__((packed));

/* LFN entry (ATTR=0x0F) */
struct fat_lfn_entry {
  uint8_t seq;
  uint16_t name1[5];
  uint8_t attr;
  uint8_t type;
  uint8_t checksum;
  uint16_t name2[6];
  uint16_t zero;
  uint16_t name3[2];
} __attribute__((packed));

static int str_casecmp(const char *a, const char *b) {
  while (*a && *b) {
    char ca = *a;
    char cb = *b;
    if (ca >= 'A' && ca <= 'Z')
      ca += 32;
    if (cb >= 'A' && cb <= 'Z')
      cb += 32;
    if (ca != cb)
      return (int)(unsigned char)ca - (int)(unsigned char)cb;
    a++;
    b++;
  }
  return (int)(unsigned char)*a - (int)(unsigned char)*b;
}

static bool is_valid_short_char(char c) {
  if (c >= 'A' && c <= 'Z')
    return true;
  if (c >= '0' && c <= '9')
    return true;
  switch (c) {
  case '!':
  case '#':
  case '$':
  case '%':
  case '&':
  case '\'':
  case '(':
  case ')':
  case '-':
  case '@':
  case '^':
  case '_':
  case '`':
  case '{':
  case '}':
  case '~':
    return true;
  default:
    return false;
  }
}

static bool needs_lfn(const char *name, int len) {
  if (len <= 0)
    return false;
  if (len > 12)
    return true;
  int dot_count = 0;
  int base_len = 0;
  int ext_len = 0;
  for (int i = 0; i < len; i++) {
    char c = name[i];
    if (c == '.') {
      dot_count++;
      continue;
    }
    if (c >= 'a' && c <= 'z')
      return true;
    if (c == ' ')
      return true;
    if (!is_valid_short_char(c))
      return true;
    if (dot_count == 0)
      base_len++;
    else
      ext_len++;
  }
  if (dot_count > 1)
    return true;
  if (base_len > 8 || ext_len > 3)
    return true;
  return false;
}

static uint8_t lfn_checksum(const char short_name[11]) {
  uint8_t sum = 0;
  for (int i = 0; i < 11; i++) {
    sum = (sum >> 1) | (sum << 7);
    sum += (uint8_t)short_name[i];
  }
  return sum;
}

static void lfn_reset(char *buf) {
  if (buf)
    memset(buf, 0, 260);
}

static void lfn_put_part(char *out, int out_len, int seq,
                         const struct fat_lfn_entry *lfn) {
  if (!out || out_len <= 0)
    return;
  int base = (seq - 1) * 13;
  int idx = base;
  for (int i = 0; i < 5; i++) {
    uint16_t ch = lfn->name1[i];
    if (idx < out_len - 1)
      out[idx++] = (char)(ch & 0xFF);
    if (ch == 0x0000 || ch == 0xFFFF)
      return;
  }
  for (int i = 0; i < 6; i++) {
    uint16_t ch = lfn->name2[i];
    if (idx < out_len - 1)
      out[idx++] = (char)(ch & 0xFF);
    if (ch == 0x0000 || ch == 0xFFFF)
      return;
  }
  for (int i = 0; i < 2; i++) {
    uint16_t ch = lfn->name3[i];
    if (idx < out_len - 1)
      out[idx++] = (char)(ch & 0xFF);
    if (ch == 0x0000 || ch == 0xFFFF)
      return;
  }
}

static void lfn_finalize(char *buf, int buf_len) {
  for (int i = 0; i < buf_len; i++) {
    unsigned char c = (unsigned char)buf[i];
    if (c == 0xFF || c == 0x00) {
      buf[i] = 0;
      return;
    }
  }
  buf[buf_len - 1] = 0;
}

static void make_short_alias(const char *longname, char out[11], int suffix) {
  memset(out, ' ', 11);
  const char *dot = strrchr(longname, '.');
  int base_len = dot ? (int)(dot - longname) : (int)strlen(longname);
  int ext_len = dot ? (int)strlen(dot + 1) : 0;
  if (base_len < 0)
    base_len = 0;
  if (ext_len < 0)
    ext_len = 0;

  int bi = 0;
  for (int i = 0; i < base_len && bi < 8; i++) {
    char c = longname[i];
    if (c == ' ' || c == '.')
      continue;
    if (c >= 'a' && c <= 'z')
      c -= 32;
    out[bi++] = c;
  }

  if (suffix > 0) {
    char suf[8];
    int si = 0;
    int tmp = suffix;
    while (tmp > 0 && si < (int)sizeof(suf) - 1) {
      suf[si++] = (char)('0' + (tmp % 10));
      tmp /= 10;
    }
    if (si == 0) {
      suf[si++] = '0';
    }
    for (int i = 0; i < si / 2; i++) {
      char t = suf[i];
      suf[i] = suf[si - 1 - i];
      suf[si - 1 - i] = t;
    }
    suf[si] = 0;
    int max_base = 8 - (1 + si);
    if (max_base < 1)
      max_base = 1;
    if (bi > max_base)
      bi = max_base;
    out[bi++] = '~';
    for (int i = 0; i < si && bi < 8; i++) {
      out[bi++] = suf[i];
    }
    while (bi < 8)
      out[bi++] = ' ';
  }

  int ei = 0;
  for (int i = 0; i < ext_len && ei < 3; i++) {
    char c = dot[1 + i];
    if (c == ' ')
      continue;
    if (c >= 'a' && c <= 'z')
      c -= 32;
    out[8 + ei++] = c;
  }
}

static void copy_component(char *out, int out_len, const char *in, int in_len) {
  if (!out || out_len <= 0)
    return;
  int n = in_len;
  if (n >= out_len)
    n = out_len - 1;
  if (n < 0)
    n = 0;
  memcpy(out, in, n);
  out[n] = 0;
}

static void lfn_build_entry(struct fat_lfn_entry *e, int seq, bool is_last,
                            uint8_t checksum, const char *name, int name_len,
                            int start) {
  e->seq = (uint8_t)seq | (is_last ? 0x40 : 0x00);
  e->attr = 0x0F;
  e->type = 0;
  e->checksum = checksum;
  e->zero = 0;

  for (int i = 0; i < 5; i++)
    e->name1[i] = 0xFFFF;
  for (int i = 0; i < 6; i++)
    e->name2[i] = 0xFFFF;
  for (int i = 0; i < 2; i++)
    e->name3[i] = 0xFFFF;

  for (int i = 0; i < 13; i++) {
    int idx = start + i;
    uint16_t ch;
    if (idx < name_len)
      ch = (uint8_t)name[idx];
    else if (idx == name_len)
      ch = 0x0000;
    else
      ch = 0xFFFF;

    if (i < 5)
      e->name1[i] = ch;
    else if (i < 11)
      e->name2[i - 5] = ch;
    else
      e->name3[i - 11] = ch;
  }
}

struct fat_fsinfo {
  uint32_t lead_sig; // 0x41615252
  uint8_t reserved1[480];
  uint32_t struc_sig;  // 0x61417272
  uint32_t free_count; // -1
  uint32_t next_free;  // 2
  uint8_t reserved2[12];
  uint32_t trail_sig; // 0xAA550000
} __attribute__((packed));

/* Convert a single filename component to 8.3 DOS name */
static void to_dos_name_component(const char *name, int len, char *dst) {
  memset(dst, ' ', 11);
  int i = 0;
  for (; i < 8 && i < len && name[i] != '.'; i++) {
    char c = name[i];
    if (c >= 'a' && c <= 'z')
      c -= 32;
    dst[i] = c;
  }
  while (i < len && name[i] != '.')
    i++;
  if (i < len && name[i] == '.') {
    i++;
    int j = 8;
    for (; j < 11 && i < len; i++, j++) {
      char c = name[i];
      if (c >= 'a' && c <= 'z')
        c -= 32;
      dst[j] = c;
    }
  }
}

/* Helper to find an entry in a directory cluster */
static int find_in_cluster(uint32_t dir_cluster, const char *name, int name_len,
                           uint32_t data_start, uint32_t fat_start,
                           uint32_t spc, uint32_t bps, uint32_t *out_cluster,
                           uint32_t *out_size, uint32_t *out_sector,
                           uint32_t *out_offset, bool *out_is_dir) {
  if (name_len <= 0 || name_len > 255)
    return -1;
  char name_buf[256];
  copy_component(name_buf, sizeof(name_buf), name, name_len);

  char target[11];
  to_dos_name_component(name, name_len, target);
  char lfn_buf[260];
  lfn_reset(lfn_buf);
  bool lfn_active = false;
  uint8_t lfn_chk = 0;

  uint8_t *sector = (uint8_t *)kmalloc(512);
  if (!sector)
    return -1;

  uint32_t current_cluster = dir_cluster;
  while (current_cluster < 0x0FFFFFF8) {
    uint32_t cluster_lba = data_start + (current_cluster - 2) * spc;
    for (int i = 0; i < (int)spc; i++) {
      disk_read_sector(cluster_lba + i, sector);
      struct fat_dir_entry *entries = (struct fat_dir_entry *)sector;
      for (int j = 0; j < 512 / 32; j++) {
        if (entries[j].name[0] == 0) {
          kfree(sector);
          return -1;
        }
        if ((uint8_t)entries[j].name[0] == 0xE5)
          continue;
        if (entries[j].attr == 0x0F) {
          struct fat_lfn_entry *lfn = (struct fat_lfn_entry *)&entries[j];
          int seq = lfn->seq & 0x1F;
          if (lfn->seq & 0x40) {
            lfn_reset(lfn_buf);
            lfn_active = true;
            lfn_chk = lfn->checksum;
          }
          if (lfn_active) {
            lfn_put_part(lfn_buf, sizeof(lfn_buf), seq, lfn);
          }
          continue;
        }

        if (lfn_active) {
          lfn_finalize(lfn_buf, sizeof(lfn_buf));
          uint8_t short_chk = lfn_checksum((const char *)entries[j].name);
          if (lfn_chk == short_chk && str_casecmp(lfn_buf, name_buf) == 0) {
            if (out_cluster) {
              *out_cluster =
                  (entries[j].cluster_hi << 16) | entries[j].cluster_low;
              if (*out_cluster == 0)
                *out_cluster = 0;
            }
            if (out_size)
              *out_size = entries[j].size;
            if (out_sector)
              *out_sector = cluster_lba + i;
            if (out_offset)
              *out_offset = j;
            if (out_is_dir)
              *out_is_dir = (entries[j].attr & 0x10) ? true : false;
            kfree(sector);
            return 0;
          }
          lfn_active = false;
        }

        if (memcmp(entries[j].name, target, 11) == 0) {
          if (out_cluster) {
            *out_cluster =
                (entries[j].cluster_hi << 16) | entries[j].cluster_low;
            if (*out_cluster == 0)
              *out_cluster = 0; // Root is 0 or 2 usually, handle carefully
          }
          if (out_size)
            *out_size = entries[j].size;
          if (out_sector)
            *out_sector = cluster_lba + i;
          if (out_offset)
            *out_offset = j;
          if (out_is_dir)
            *out_is_dir = (entries[j].attr & 0x10) ? true : false;
          kfree(sector);
          return 0;
        }
      }
    }

    /* Next cluster */
    uint32_t fat_sector = fat_start + (current_cluster * 4) / bps;
    uint32_t fat_offset = (current_cluster * 4) % bps;
    disk_read_sector(fat_sector, sector);
    current_cluster = (*(uint32_t *)(sector + fat_offset)) & 0x0FFFFFFF;
  }
  kfree(sector);
  return -1;
}

static bool short_name_exists(const uint8_t *dirbuf, int total_entries,
                              const char short_name[11]) {
  const struct fat_dir_entry *entries = (const struct fat_dir_entry *)dirbuf;
  for (int i = 0; i < total_entries; i++) {
    if (entries[i].name[0] == 0)
      break;
    if ((uint8_t)entries[i].name[0] == 0xE5)
      continue;
    if (entries[i].attr == 0x0F)
      continue;
    if (memcmp(entries[i].name, short_name, 11) == 0)
      return true;
  }
  return false;
}

static uint32_t fat_get_next_cluster(uint32_t cluster, uint32_t fat_start,
                                     uint16_t bps, uint8_t *sector);

static bool short_name_exists_in_dir(uint32_t dir_cluster, uint32_t data_start,
                                     uint32_t fat_start, uint8_t spc,
                                     uint16_t bps, const char short_name[11]) {
  uint8_t *sector = (uint8_t *)kmalloc(512);
  if (!sector)
    return false;
  int entries_per_sector = 512 / 32;
  uint32_t current_cluster = dir_cluster;
  while (current_cluster < 0x0FFFFFF8) {
    uint32_t cluster_lba = data_start + (current_cluster - 2) * spc;
    for (int i = 0; i < spc; i++) {
      disk_read_sector(cluster_lba + i, sector);
      if (short_name_exists(sector, entries_per_sector, short_name)) {
        kfree(sector);
        return true;
      }
    }
    current_cluster =
        fat_get_next_cluster(current_cluster, fat_start, bps, sector);
  }
  kfree(sector);
  return false;
}

static uint32_t fat_get_next_cluster(uint32_t cluster, uint32_t fat_start,
                                     uint16_t bps, uint8_t *sector) {
  uint32_t fat_sector = fat_start + (cluster * 4) / bps;
  uint32_t fat_offset = (cluster * 4) % bps;
  if (disk_read_sector(fat_sector, sector) != 0) {
    serial("[FAT] fat_get_next_cluster: read failed LBA %d\n", (int)fat_sector);
    return 0x0FFFFFFF;
  }
  return (*(uint32_t *)(sector + fat_offset)) & 0x0FFFFFFF;
}

static void fat_set_next_cluster(uint32_t cluster, uint32_t value,
                                 uint32_t fat_start, uint16_t bps,
                                 uint8_t *sector) {
  uint32_t fat_sector = fat_start + (cluster * 4) / bps;
  uint32_t fat_offset = (cluster * 4) % bps;
  if (disk_read_sector(fat_sector, sector) != 0) {
    serial("[FAT] fat_set_next_cluster: read failed LBA %d\n", (int)fat_sector);
    return;
  }
  *(uint32_t *)(sector + fat_offset) = value;
  if (disk_write_sector(fat_sector, sector) != 0) {
    serial("[FAT] fat_set_next_cluster: write failed LBA %d\n",
           (int)fat_sector);
  }
}

static uint32_t fat_alloc_cluster(uint32_t fat_start, uint32_t sectors_per_fat,
                                  uint8_t *sector) {
  for (uint32_t s = 0; s < sectors_per_fat; s++) {
    if (disk_read_sector(fat_start + s, sector) != 0) {
      serial("[FAT] fat_alloc_cluster: read failed LBA %d\n",
             (int)(fat_start + s));
      continue;
    }
    uint32_t *table = (uint32_t *)sector;
    for (int k = 0; k < 128; k++) {
      if ((table[k] & 0x0FFFFFFF) == 0) {
        uint32_t cluster = s * 128 + k;
        if (cluster < 2)
          continue;
        table[k] = 0x0FFFFFFF;
        if (disk_write_sector(fat_start + s, sector) != 0) {
          serial("[FAT] fat_alloc_cluster: write failed LBA %d\n",
                 (int)(fat_start + s));
          return 0;
        }
        return cluster;
      }
    }
  }
  return 0;
}

static uint32_t fat_alloc_cluster_from(uint32_t fat_start,
                                       uint32_t sectors_per_fat,
                                       uint8_t *sector,
                                       uint32_t *hint_cluster) {
  uint32_t start = (hint_cluster && *hint_cluster >= 2) ? *hint_cluster : 2;
  uint32_t total_entries = sectors_per_fat * 128;
  if (start >= total_entries)
    start = 2;

  for (int pass = 0; pass < 2; pass++) {
    uint32_t first = (pass == 0) ? start : 2;
    uint32_t last = (pass == 0) ? total_entries : start;
    for (uint32_t cluster = first; cluster < last; cluster++) {
      uint32_t s = cluster / 128;
      uint32_t k = cluster % 128;
      if (disk_read_sector(fat_start + s, sector) != 0) {
        serial("[FAT] fat_alloc_cluster_from: read failed LBA %d\n",
               (int)(fat_start + s));
        continue;
      }
      uint32_t *table = (uint32_t *)sector;
      if ((table[k] & 0x0FFFFFFF) == 0) {
        table[k] = 0x0FFFFFFF;
        if (disk_write_sector(fat_start + s, sector) != 0) {
          serial("[FAT] fat_alloc_cluster_from: write failed LBA %d\n",
                 (int)(fat_start + s));
          return 0;
        }
        if (hint_cluster)
          *hint_cluster = cluster + 1;
        return cluster;
      }
    }
  }
  return 0;
}

static int fat_set_next_cluster_checked(uint32_t cluster, uint32_t value,
                                        uint32_t fat_start, uint16_t bps,
                                        uint8_t *sector, int verify) {
  uint32_t fat_sector = fat_start + (cluster * 4) / bps;
  uint32_t fat_offset = (cluster * 4) % bps;
  if (disk_read_sector(fat_sector, sector) != 0) {
    serial("[FAT] fat_set_next_cluster_checked: read failed LBA %d\n",
           (int)fat_sector);
    return -1;
  }
  *(uint32_t *)(sector + fat_offset) = value;
  if (disk_write_sector(fat_sector, sector) != 0) {
    serial("[FAT] fat_set_next_cluster_checked: write failed LBA %d\n",
           (int)fat_sector);
    return -1;
  }
  if (verify) {
    uint8_t *verify_buf = (uint8_t *)kmalloc(512);
    if (!verify_buf) {
      serial(
          "[FAT] fat_set_next_cluster_checked: no verify buffer, skipping\n");
      return 0;
    }
    if (disk_read_sector(fat_sector, verify_buf) != 0) {
      serial("[FAT] fat_set_next_cluster_checked: verify read failed LBA %d\n",
             (int)fat_sector);
      kfree(verify_buf);
      return -1;
    }
    uint32_t v = (*(uint32_t *)(verify_buf + fat_offset)) & 0x0FFFFFFF;
    kfree(verify_buf);
    if (v != (value & 0x0FFFFFFF)) {
      serial("[FAT] fat_set_next_cluster_checked: verify mismatch LBA %d\n",
             (int)fat_sector);
      return -1;
    }
  }
  return 0;
}

static bool dir_locate_entry(uint32_t dir_cluster, uint32_t entry_idx,
                             uint32_t data_start, uint32_t fat_start,
                             uint8_t spc, uint16_t bps,
                             uint32_t *out_sector_lba, uint32_t *out_offset) {
  int entries_per_sector = 512 / 32;
  int entries_per_cluster = spc * entries_per_sector;
  uint32_t cluster_steps = entry_idx / entries_per_cluster;
  uint32_t offset_in_cluster = entry_idx % entries_per_cluster;
  uint32_t sector_idx = offset_in_cluster / entries_per_sector;
  uint32_t offset = offset_in_cluster % entries_per_sector;

  uint8_t *sector = (uint8_t *)kmalloc(512);
  if (!sector)
    return false;

  uint32_t cluster = dir_cluster;
  for (uint32_t i = 0; i < cluster_steps; i++) {
    cluster = fat_get_next_cluster(cluster, fat_start, bps, sector);
    if (cluster >= 0x0FFFFFF8) {
      kfree(sector);
      return false;
    }
  }

  *out_sector_lba = data_start + (cluster - 2) * spc + sector_idx;
  *out_offset = offset;
  kfree(sector);
  return true;
}

static int dir_calc_entry_index(uint32_t dir_cluster, uint32_t target_sector,
                                uint32_t target_offset, uint32_t data_start,
                                uint32_t fat_start, uint8_t spc, uint16_t bps) {
  int entries_per_sector = 512 / 32;
  uint8_t *sector = (uint8_t *)kmalloc(512);
  if (!sector)
    return -1;
  uint32_t current_cluster = dir_cluster;
  int index = 0;
  while (current_cluster < 0x0FFFFFF8) {
    uint32_t cluster_lba = data_start + (current_cluster - 2) * spc;
    for (int i = 0; i < spc; i++) {
      uint32_t lba = cluster_lba + i;
      disk_read_sector(lba, sector);
      if (lba == target_sector) {
        kfree(sector);
        return index + (int)target_offset;
      }
      index += entries_per_sector;
    }
    current_cluster =
        fat_get_next_cluster(current_cluster, fat_start, bps, sector);
  }
  kfree(sector);
  return -1;
}

static bool dir_find_free_run(uint32_t dir_cluster, uint32_t data_start,
                              uint32_t fat_start, uint8_t spc, uint16_t bps,
                              int need_entries, uint32_t *out_sector_lba,
                              uint32_t *out_offset, int *out_run_start) {
  if (need_entries <= 0)
    return false;
  int entries_per_sector = 512 / 32;
  uint8_t *sector = (uint8_t *)kmalloc(512);
  if (!sector)
    return false;

  int run_start = -1;
  int run_len = 0;
  int entry_index = 0;
  uint32_t current_cluster = dir_cluster;

  while (current_cluster < 0x0FFFFFF8) {
    uint32_t cluster_lba = data_start + (current_cluster - 2) * spc;
    for (int i = 0; i < spc; i++) {
      uint32_t lba = cluster_lba + i;
      disk_read_sector(lba, sector);
      struct fat_dir_entry *entries = (struct fat_dir_entry *)sector;
      for (int j = 0; j < entries_per_sector; j++, entry_index++) {
        uint8_t first = entries[j].name[0];
        bool free = (first == 0 || first == 0xE5);
        if (free) {
          if (run_start < 0)
            run_start = entry_index;
          run_len++;
          if (run_len >= need_entries) {
            *out_sector_lba = lba;
            *out_offset = j;
            if (out_run_start)
              *out_run_start = run_start;
            kfree(sector);
            return true;
          }
        } else {
          run_start = -1;
          run_len = 0;
        }
      }
    }
    current_cluster =
        fat_get_next_cluster(current_cluster, fat_start, bps, sector);
  }

  kfree(sector);
  return false;
}

static void dir_mark_lfn_deleted(uint32_t parent_cluster, int entry_idx,
                                 uint32_t data_start, uint32_t fat_start,
                                 uint8_t spc, uint16_t bps) {
  if (entry_idx <= 0)
    return;
  uint8_t *sector = (uint8_t *)kmalloc(512);
  if (!sector)
    return;
  for (int idx = entry_idx - 1; idx >= 0; idx--) {
    uint32_t lfn_sector;
    uint32_t lfn_offset;
    if (!dir_locate_entry(parent_cluster, (uint32_t)idx, data_start, fat_start,
                          spc, bps, &lfn_sector, &lfn_offset)) {
      break;
    }
    disk_read_sector(lfn_sector, sector);
    struct fat_dir_entry *entries = (struct fat_dir_entry *)sector;
    if (entries[lfn_offset].attr != 0x0F)
      break;
    entries[lfn_offset].name[0] = 0xE5;
    disk_write_sector(lfn_sector, sector);
  }
  kfree(sector);
}

static int write_sector_verified(uint32_t lba, const uint8_t *buf, int verify);

static int dir_create_entry_with_cluster(uint32_t parent_cluster,
                                         const char *name_buf, int name_len,
                                         uint32_t cluster, uint32_t size,
                                         uint8_t attr, uint32_t data_start,
                                         uint32_t fat_start, uint8_t spc,
                                         uint16_t bps, uint32_t sectors_per_fat,
                                         int verify) {
  uint8_t *sector = (uint8_t *)kmalloc(512);
  if (!sector) {
    serial("[FAT] mkdir: no memory for sector buffer\n");
    return -20;
  }
  uint8_t *verify_buf = 0;
  if (verify) {
    verify_buf = (uint8_t *)kmalloc(512);
    if (!verify_buf) {
      serial("[FAT] mkdir: no memory for verify buffer\n");
    }
  }

  int lfn_entries = 0;
  bool need_lfn = needs_lfn(name_buf, name_len);
  if (need_lfn)
    lfn_entries = (name_len + 12) / 13;
  int need_entries = lfn_entries + 1;

  uint32_t entry_sector_lba = 0;
  uint32_t entry_offset = 0;
  int free_run_start = -1;

  if (!dir_find_free_run(parent_cluster, data_start, fat_start, spc, bps,
                         need_entries, &entry_sector_lba, &entry_offset,
                         &free_run_start)) {
    /* Extend directory by one cluster and retry */
    uint8_t *fatbuf = (uint8_t *)kmalloc(512);
    if (!fatbuf) {
      serial("[FAT] mkdir: no memory for fatbuf\n");
      if (verify_buf)
        kfree(verify_buf);
      kfree(sector);
      return -20;
    }
    uint32_t current = parent_cluster;
    int cluster_index = 0;
    while (true) {
      uint32_t next = fat_get_next_cluster(current, fat_start, bps, fatbuf);
      if (next >= 0x0FFFFFF8)
        break;
      current = next;
      cluster_index++;
    }
    uint32_t new_cluster =
        fat_alloc_cluster(fat_start, sectors_per_fat, fatbuf);
    if (new_cluster == 0) {
      kfree(fatbuf);
      if (verify_buf)
        kfree(verify_buf);
      kfree(sector);
      return -24;
    }
    fat_set_next_cluster(current, new_cluster, fat_start, bps, fatbuf);
    fat_set_next_cluster(new_cluster, 0x0FFFFFFF, fat_start, bps, fatbuf);

    memset(sector, 0, 512);
    uint32_t new_lba = data_start + (new_cluster - 2) * spc;
    for (int i = 0; i < spc; i++) {
      if (write_sector_verified(new_lba + i, sector, verify) != 0) {
        serial("[FAT] mkdir: extend dir write failed LBA %d\n",
               (int)(new_lba + i));
        kfree(fatbuf);
        if (verify_buf)
          kfree(verify_buf);
        kfree(sector);
        return -24;
      }
    }
    kfree(fatbuf);

    int entries_per_sector = 512 / 32;
    int entries_per_cluster = spc * entries_per_sector;
    free_run_start = (cluster_index + 1) * entries_per_cluster;
    uint32_t short_idx = free_run_start + need_entries - 1;
    if (!dir_locate_entry(parent_cluster, short_idx, data_start, fat_start, spc,
                          bps, &entry_sector_lba, &entry_offset)) {
      if (verify_buf)
        kfree(verify_buf);
      kfree(sector);
      return -23;
    }
  } else if (free_run_start < 0) {
    if (verify_buf)
      kfree(verify_buf);
    kfree(sector);
    return -23;
  }

  char short_name[11];
  if (!need_lfn) {
    to_dos_name_component(name_buf, name_len, short_name);
  } else {
    int suffix = 0;
    while (suffix <= 9999) {
      make_short_alias(name_buf, short_name, suffix);
      if (!short_name_exists_in_dir(parent_cluster, data_start, fat_start, spc,
                                    bps, short_name))
        break;
      suffix++;
    }
    if (suffix > 9999) {
      serial("[FAT] mkdir: cannot create unique short alias\n");
      if (verify_buf)
        kfree(verify_buf);
      kfree(sector);
      return -25;
    }
  }

  if (need_lfn) {
    uint8_t checksum = lfn_checksum(short_name);
    for (int i = 0; i < lfn_entries; i++) {
      int seq = i + 1;
      bool is_last = (i == lfn_entries - 1);
      struct fat_lfn_entry lfn;
      lfn_build_entry(&lfn, seq, is_last, checksum, name_buf, name_len, i * 13);

      int entry_idx = free_run_start + (lfn_entries - 1 - i);
      uint32_t lfn_sector;
      uint32_t lfn_offset;
      if (!dir_locate_entry(parent_cluster, (uint32_t)entry_idx, data_start,
                            fat_start, spc, bps, &lfn_sector, &lfn_offset)) {
        serial("[FAT] mkdir: dir_locate_entry failed for LFN\n");
        if (verify_buf)
          kfree(verify_buf);
        kfree(sector);
        return -21;
      }
      if (disk_read_sector(lfn_sector, sector) != 0) {
        serial("[FAT] mkdir: LFN sector read failed LBA %d\n", (int)lfn_sector);
        if (verify_buf)
          kfree(verify_buf);
        kfree(sector);
        return -26;
      }
      struct fat_dir_entry *entries = (struct fat_dir_entry *)sector;
      memcpy(&entries[lfn_offset], &lfn, sizeof(lfn));
      if (write_sector_verified(lfn_sector, sector, verify) != 0) {
        serial("[FAT] mkdir: LFN write failed LBA %d\n", (int)lfn_sector);
        if (verify_buf)
          kfree(verify_buf);
        kfree(sector);
        return -21;
      }
    }
  }

  if (disk_read_sector(entry_sector_lba, sector) != 0) {
    serial("[FAT] mkdir: short entry sector read failed LBA %d\n",
           (int)entry_sector_lba);
    if (verify_buf)
      kfree(verify_buf);
    kfree(sector);
    return -27;
  }
  struct fat_dir_entry *entries = (struct fat_dir_entry *)sector;
  struct fat_dir_entry *entry = &entries[entry_offset];
  memcpy(entry->name, short_name, 11);
  entry->attr = attr;
  entry->cluster_hi = (cluster >> 16);
  entry->cluster_low = (cluster & 0xFFFF);
  entry->size = size;
  if (write_sector_verified(entry_sector_lba, sector, verify) != 0) {
    serial("[FAT] mkdir: short entry write failed LBA %d\n",
           (int)entry_sector_lba);
    if (verify_buf)
      kfree(verify_buf);
    kfree(sector);
    return -22;
  }

  if (verify_buf)
    kfree(verify_buf);
  kfree(sector);
  return 0;
}

/* Resolve path to parent directory cluster and final filename component */
static int resolve_parent(const char *path, uint32_t root_cluster,
                          uint32_t data_start, uint32_t fat_start, uint32_t spc,
                          uint32_t bps, uint32_t *out_parent_cluster,
                          const char **out_filename, int *out_filename_len) {
  const char *p = path;
  if (*p == '/')
    p++;

  uint32_t curr_cluster = root_cluster;

  while (*p) {
    const char *end = p;
    while (*end && *end != '/')
      end++;
    int len = end - p;
    if (len > 255)
      return -1;

    if (*end == 0) {
      /* Last component */
      *out_parent_cluster = curr_cluster;
      *out_filename = p;
      *out_filename_len = len;
      return 0;
    }

    /* Find directory */
    uint32_t next_cluster;
    bool is_dir;
    if (find_in_cluster(curr_cluster, p, len, data_start, fat_start, spc, bps,
                        &next_cluster, NULL, NULL, NULL, &is_dir) != 0) {
      return -1; /* Path not found */
    }
    if (!is_dir)
      return -1; /* Not a directory */

    curr_cluster = next_cluster;
    if (curr_cluster == 0)
      curr_cluster = root_cluster;
    p = end + 1;
  }
  return -1;
}

/* --- FAT32 File Operations --- */

extern "C" int fat32_read_file(const char *path, void *buf, uint32_t max_size) {
  if (!is_fat_initialized)
    return -1;

  uint8_t *sector = (uint8_t *)kmalloc(512);
  if (!sector)
    return -1;

  /* 1. Read BPB */
  if (disk_read_sector(current_lba, sector) != 0) {
    kfree(sector);
    return -1;
  }
  struct fat_bpb *bpb = (struct fat_bpb *)sector;

  if (bpb->bytes_per_sector == 0) {
    terminal_writestring("Error: Invalid FAT32 BPB (bytes_per_sector=0).\n");
    kfree(sector);
    return -1;
  }

  uint32_t fat_start = current_lba + bpb->reserved_sectors;
  uint32_t data_start = fat_start + (bpb->fats_count * bpb->sectors_per_fat_32);
  uint32_t root_cluster = bpb->root_cluster;
  uint8_t spc = bpb->sectors_per_cluster;
  uint16_t bps = bpb->bytes_per_sector;

  /* 2. Resolve Path */
  uint32_t file_cluster = 0;
  uint32_t file_size = 0;
  uint32_t parent_cluster;
  const char *fname;
  int fname_len;

  if (resolve_parent(path, root_cluster, data_start, fat_start, spc, bps,
                     &parent_cluster, &fname, &fname_len) != 0) {
    kfree(sector);
    return -1;
  }

  bool is_dir;
  if (find_in_cluster(parent_cluster, fname, fname_len, data_start, fat_start,
                      spc, bps, &file_cluster, &file_size, NULL, NULL,
                      &is_dir) != 0) {
    kfree(sector);
    return -1;
  }

  if (is_dir) {
    /* Cannot read directory as file */
    kfree(sector);
    return -1;
  }

  /* 3. Read File Data */
  uint32_t bytes_read = 0;
  uint8_t *out = (uint8_t *)buf;
  uint32_t current_cluster = file_cluster;

  while (bytes_read < file_size && bytes_read < max_size) {
    uint32_t cluster_lba = data_start + (current_cluster - 2) * spc;

    for (int i = 0; i < spc; i++) {
      disk_read_sector(cluster_lba + i, sector);
      uint32_t chunk =
          (file_size - bytes_read > 512) ? 512 : (file_size - bytes_read);
      if (bytes_read + chunk > max_size)
        chunk = max_size - bytes_read;

      memcpy(out + bytes_read, sector, chunk);
      bytes_read += chunk;
      if (bytes_read >= file_size || bytes_read >= max_size)
        break;
    }

    /* Get next cluster from FAT */
    uint32_t fat_sector = fat_start + (current_cluster * 4) / bps;
    uint32_t fat_offset = (current_cluster * 4) % bps;

    disk_read_sector(fat_sector, sector);
    current_cluster = (*(uint32_t *)(sector + fat_offset)) & 0x0FFFFFFF;

    if (current_cluster >= 0x0FFFFFF8)
      break; /* EOC */
  }

  kfree(sector);
  return bytes_read;
}

extern "C" int fat32_read_file_offset(const char *path, void *buf,
                                      uint32_t size, uint32_t offset) {
  if (!is_fat_initialized)
    return -1;

  uint8_t *sector = (uint8_t *)kmalloc(512);
  if (!sector)
    return -1;

  /* 1. Read BPB */
  if (disk_read_sector(current_lba, sector) != 0) {
    kfree(sector);
    return -1;
  }
  struct fat_bpb *bpb = (struct fat_bpb *)sector;

  if (bpb->bytes_per_sector == 0) {
    kfree(sector);
    return -1;
  }

  uint32_t fat_start = current_lba + bpb->reserved_sectors;
  uint32_t data_start = fat_start + (bpb->fats_count * bpb->sectors_per_fat_32);
  uint32_t root_cluster = bpb->root_cluster;
  uint8_t spc = bpb->sectors_per_cluster;
  uint16_t bps = bpb->bytes_per_sector;
  uint32_t cluster_bytes = spc * bps;

  /* 2. Resolve Path */
  uint32_t file_cluster = 0;
  uint32_t file_size = 0;
  uint32_t parent_cluster;
  const char *fname;
  int fname_len;

  if (resolve_parent(path, root_cluster, data_start, fat_start, spc, bps,
                     &parent_cluster, &fname, &fname_len) != 0) {
    kfree(sector);
    return -1;
  }

  bool is_dir;
  if (find_in_cluster(parent_cluster, fname, fname_len, data_start, fat_start,
                      spc, bps, &file_cluster, &file_size, NULL, NULL,
                      &is_dir) != 0) {
    kfree(sector);
    return -1;
  }

  if (offset >= file_size) {
    kfree(sector);
    return 0;
  }
  if (offset + size > file_size)
    size = file_size - offset;

  /* 3. Seek to cluster */
  uint32_t current_cluster = file_cluster;
  uint32_t clusters_to_skip = offset / cluster_bytes;
  uint32_t offset_in_cluster = offset % cluster_bytes;

  for (uint32_t i = 0; i < clusters_to_skip; i++) {
    uint32_t fat_sector = fat_start + (current_cluster * 4) / bps;
    uint32_t fat_offset = (current_cluster * 4) % bps;
    disk_read_sector(fat_sector, sector);
    current_cluster = (*(uint32_t *)(sector + fat_offset)) & 0x0FFFFFFF;
    if (current_cluster >= 0x0FFFFFF8) {
      kfree(sector);
      return -1;
    }
  }

  /* 4. Read Data */
  uint32_t bytes_read = 0;
  uint8_t *out = (uint8_t *)buf;

  while (bytes_read < size) {
    uint32_t cluster_lba = data_start + (current_cluster - 2) * spc;
    uint32_t cluster_offset = (bytes_read == 0) ? offset_in_cluster : 0;
    uint32_t start_sector = cluster_offset / bps;
    uint32_t sector_offset = cluster_offset % bps;

    for (int i = start_sector; i < spc && bytes_read < size; i++) {
      disk_read_sector(cluster_lba + i, sector);
      uint32_t available = bps - sector_offset;
      uint32_t to_copy =
          (size - bytes_read > available) ? available : (size - bytes_read);
      memcpy(out + bytes_read, sector + sector_offset, to_copy);
      bytes_read += to_copy;
      sector_offset = 0;
    }

    uint32_t fat_sector = fat_start + (current_cluster * 4) / bps;
    uint32_t fat_offset = (current_cluster * 4) % bps;
    disk_read_sector(fat_sector, sector);
    current_cluster = (*(uint32_t *)(sector + fat_offset)) & 0x0FFFFFFF;
    if (current_cluster >= 0x0FFFFFF8)
      break;
  }

  kfree(sector);
  return bytes_read;
}

static int fat32_create_file_impl(const char *path, const void *data,
                                  uint32_t size, int verify, int skip_write) {
  if (!is_fat_initialized)
    return -1;

  uint8_t *sector = (uint8_t *)kmalloc(512);
  if (!sector)
    return -1;

  /* 1. Read BPB */
  disk_read_sector(current_lba, sector);
  struct fat_bpb *bpb_ptr = (struct fat_bpb *)sector;

  if (bpb_ptr->bytes_per_sector == 0) {
    terminal_writestring("Error: Invalid FAT32 BPB (bytes_per_sector=0).\n");
    kfree(sector);
    return -1;
  }

  /* Copy essential values before buffer is reused */
  uint16_t b_bps = bpb_ptr->bytes_per_sector;
  uint8_t b_spc = bpb_ptr->sectors_per_cluster;
  uint32_t b_res = bpb_ptr->reserved_sectors;
  uint8_t b_fats = bpb_ptr->fats_count;
  uint32_t b_spf = bpb_ptr->sectors_per_fat_32;
  uint32_t b_root = bpb_ptr->root_cluster;

  uint32_t fat_start = current_lba + b_res;
  uint32_t data_start = fat_start + (b_fats * b_spf);
  uint32_t root_cluster = b_root;
  uint8_t spc = b_spc;
  uint32_t sectors_per_fat = b_spf;
  uint16_t bps = b_bps;

  /* 2. Resolve Parent Directory */
  uint32_t parent_cluster;
  const char *fname;
  int fname_len;
  if (resolve_parent(path, root_cluster, data_start, fat_start, spc, bps,
                     &parent_cluster, &fname, &fname_len) != 0) {
    serial("[FAT] create_file: resolve_parent failed for %s\n", path);
    kfree(sector);
    return -1;
  }

  if (fname_len <= 0 || fname_len > 255) {
    serial("[FAT] create_file: invalid name length %d for %s\n", fname_len,
           path);
    kfree(sector);
    return -1;
  }
  char name_buf[256];
  copy_component(name_buf, sizeof(name_buf), fname, fname_len);

  uint32_t entry_sector_lba = 0;
  uint32_t entry_offset = 0;
  bool found_existing = false;
  uint32_t file_cluster = 0;
  int free_run_start = -1;
  int lfn_entries = 0;
  bool need_lfn = needs_lfn(name_buf, fname_len);
  if (need_lfn)
    lfn_entries = (fname_len + 12) / 13;
  int need_entries = lfn_entries + 1;
  char short_name[11];

  /* Check for existing entry by long/short name */
  bool is_dir = false;
  if (find_in_cluster(parent_cluster, fname, fname_len, data_start, fat_start,
                      spc, bps, &file_cluster, NULL, &entry_sector_lba,
                      &entry_offset, &is_dir) == 0) {
    if (is_dir) {
      kfree(sector);
      return -1;
    }
    found_existing = true;
  }

  if (!found_existing) {
    if (!dir_find_free_run(parent_cluster, data_start, fat_start, spc, bps,
                           need_entries, &entry_sector_lba, &entry_offset,
                           &free_run_start)) {
      /* Extend directory by one cluster and retry */
      uint32_t new_cluster = 0;
      uint8_t *fatbuf = (uint8_t *)kmalloc(512);
      if (!fatbuf) {
        kfree(sector);
        return -1;
      }
      uint32_t current = parent_cluster;
      int cluster_index = 0;
      while (true) {
        uint32_t next = fat_get_next_cluster(current, fat_start, bps, fatbuf);
        if (next >= 0x0FFFFFF8)
          break;
        current = next;
        cluster_index++;
      }
      new_cluster = fat_alloc_cluster(fat_start, sectors_per_fat, fatbuf);
      if (new_cluster == 0) {
        serial("[FAT] create_file: no space to extend directory\n");
        kfree(fatbuf);
        kfree(sector);
        return -1;
      }
      fat_set_next_cluster(current, new_cluster, fat_start, bps, fatbuf);
      fat_set_next_cluster(new_cluster, 0x0FFFFFFF, fat_start, bps, fatbuf);

      /* zero new dir cluster */
      memset(sector, 0, 512);
      uint32_t new_lba = data_start + (new_cluster - 2) * spc;
      for (int i = 0; i < spc; i++) {
        disk_write_sector(new_lba + i, sector);
      }
      kfree(fatbuf);

      int entries_per_sector = 512 / 32;
      int entries_per_cluster = spc * entries_per_sector;
      free_run_start = (cluster_index + 1) * entries_per_cluster;
      uint32_t short_idx = free_run_start + need_entries - 1;
      if (!dir_locate_entry(parent_cluster, short_idx, data_start, fat_start,
                            spc, bps, &entry_sector_lba, &entry_offset)) {
        serial("[FAT] create_file: dir_locate_entry failed\n");
        kfree(sector);
        return -1;
      }
    }
  }

  /* 3. Allocate Cluster Chain */
  /* If file exists, free its cluster chain first. */
  if (found_existing && file_cluster != 0) {
    uint32_t current = file_cluster;
    while (current < 0x0FFFFFF8 && current != 0) {
      uint32_t next = fat_get_next_cluster(current, fat_start, bps, sector);
      fat_set_next_cluster(current, 0, fat_start, bps, sector);
      current = next;
    }
    file_cluster = 0;
  }

  uint32_t cluster_bytes = spc * bps;
  uint32_t need_clusters = (size + cluster_bytes - 1) / cluster_bytes;
  if (need_clusters == 0)
    need_clusters = 1;

  uint32_t alloc_hint = 2;
  file_cluster =
      fat_alloc_cluster_from(fat_start, sectors_per_fat, sector, &alloc_hint);
  if (file_cluster == 0) {
    serial("[FAT] create_file: no free clusters\n");
    kfree(sector);
    return -2;
  }

  uint32_t current_cluster = file_cluster;
  if (need_clusters > 256) {
    serial("[FAT] create_file: allocating %d clusters\n", (int)need_clusters);
  }
  for (uint32_t i = 1; i < need_clusters; i++) {
    uint32_t next_cluster =
        fat_alloc_cluster_from(fat_start, sectors_per_fat, sector, &alloc_hint);
    if (next_cluster == 0) {
      serial("[FAT] create_file: no free clusters for chain\n");
      /* free allocated chain */
      uint32_t cur = file_cluster;
      while (cur < 0x0FFFFFF8 && cur != 0) {
        uint32_t nxt = fat_get_next_cluster(cur, fat_start, bps, sector);
        fat_set_next_cluster(cur, 0, fat_start, bps, sector);
        cur = nxt;
      }
      kfree(sector);
      return -2;
    }
    fat_set_next_cluster(current_cluster, next_cluster, fat_start, bps, sector);
    current_cluster = next_cluster;
    if ((i % 512) == 0) {
      serial("[FAT] create_file: allocated %d/%d clusters\n", (int)i,
             (int)need_clusters);
    }
  }
  fat_set_next_cluster(current_cluster, 0x0FFFFFFF, fat_start, bps, sector);

  /* Prepare short name */
  if (found_existing) {
    disk_read_sector(entry_sector_lba, sector);
    struct fat_dir_entry *entries = (struct fat_dir_entry *)sector;
    memcpy(short_name, entries[entry_offset].name, 11);
  } else {
    if (!need_lfn) {
      to_dos_name_component(name_buf, fname_len, short_name);
    } else {
      int suffix = 0;
      while (suffix <= 9999) {
        make_short_alias(name_buf, short_name, suffix);
        if (!short_name_exists_in_dir(parent_cluster, data_start, fat_start,
                                      spc, bps, short_name))
          break;
        suffix++;
      }
      if (suffix > 9999) {
        serial("[FAT] create_file: cannot create unique short alias\n");
        kfree(sector);
        return -1;
      }
    }
  }

  /* 4. Write Data (full chain) */
  const uint8_t *data_ptr = (const uint8_t *)data;
  uint32_t written = 0;
  uint32_t data_cluster = file_cluster;
  uint32_t clusters_written = 0;
  uint32_t total_sectors = (size + 511) / 512;
  uint32_t sectors_done = 0;
  if (!skip_write) {
    serial("[FAT] create_file: writing %d bytes (%d sectors)\n", (int)size,
           (int)total_sectors);
  }
  uint8_t *verify_buf = 0;
  if (verify && !skip_write)
    verify_buf = (uint8_t *)kmalloc(512);
  while (!skip_write && data_cluster < 0x0FFFFFF8 && written < size) {
    if (data_cluster < 2) {
      serial("[FAT] create_file: invalid cluster %d\n", (int)data_cluster);
      if (verify_buf)
        kfree(verify_buf);
      kfree(sector);
      return -9;
    }
    uint32_t cluster_lba = data_start + (data_cluster - 2) * spc;
    if (sectors_done == 0) {
      serial("[FAT] create_file: first cluster LBA %d\n", (int)cluster_lba);
    }
    for (int i = 0; i < spc; i++) {
      memset(sector, 0, 512);
      uint32_t chunk = (size - written > 512) ? 512 : (size - written);
      if (chunk > 0)
        memcpy(sector, data_ptr + written, chunk);
      if (sectors_done == 0) {
        serial("[FAT] create_file: write LBA %d\n", (int)(cluster_lba + i));
      }
      int retries = verify ? 3 : 1;
      int ok = 0;
      for (int r = 0; r < retries; r++) {
        if (disk_write_sector(cluster_lba + i, sector) != 0) {
          serial("[FAT] create_file: write failed at LBA %d\n",
                 (int)(cluster_lba + i));
        } else if (!verify) {
          ok = 1;
          break;
        } else {
          if (disk_read_sector(cluster_lba + i, verify_buf) == 0 &&
              memcmp(sector, verify_buf, 512) == 0) {
            ok = 1;
            break;
          }
          serial("[FAT] create_file: verify failed at LBA %d\n",
                 (int)(cluster_lba + i));
        }
      }
      if (!ok) {
        if (verify_buf)
          kfree(verify_buf);
        kfree(sector);
        return -7;
      }
      written += chunk;
      sectors_done++;
      if ((sectors_done % 64) == 0) {
        serial("[FAT] create_file: wrote %d/%d sectors\n", (int)sectors_done,
               (int)total_sectors);
      }
      if (written >= size)
        break;
    }
    if (written >= size)
      break;
    data_cluster = fat_get_next_cluster(data_cluster, fat_start, bps, sector);
    clusters_written++;
    if (clusters_written > need_clusters + 2) {
      serial("[FAT] create_file: cluster chain too long\n");
      if (verify_buf)
        kfree(verify_buf);
      kfree(sector);
      return -8;
    }
  }
  if (!skip_write && written < size) {
    serial("[FAT] create_file: incomplete write %d/%d bytes\n", (int)written,
           (int)size);
    if (verify_buf)
      kfree(verify_buf);
    kfree(sector);
    return -10;
  }
  if (!skip_write) {
    serial("[FAT] create_file: write done (%d bytes)\n", (int)written);
    if (verify_buf)
      kfree(verify_buf);
  }

  /* 5. Update Directory Entry (and LFN if needed) */
  if (!found_existing && need_lfn) {
    uint8_t checksum = lfn_checksum(short_name);
    for (int i = 0; i < lfn_entries; i++) {
      int seq = i + 1;
      bool is_last = (i == lfn_entries - 1);
      struct fat_lfn_entry lfn;
      lfn_build_entry(&lfn, seq, is_last, checksum, name_buf, fname_len,
                      i * 13);

      int entry_idx = free_run_start + (lfn_entries - 1 - i);
      uint32_t lfn_sector;
      uint32_t lfn_offset;
      if (!dir_locate_entry(parent_cluster, entry_idx, data_start, fat_start,
                            spc, bps, &lfn_sector, &lfn_offset)) {
        serial("[FAT] create_file: dir_locate_entry failed for LFN\n");
        kfree(sector);
        return -1;
      }
      disk_read_sector(lfn_sector, sector);
      struct fat_dir_entry *entries = (struct fat_dir_entry *)sector;
      memcpy(&entries[lfn_offset], &lfn, sizeof(lfn));
      if (disk_write_sector(lfn_sector, sector) != 0) {
        serial("[FAT] create_file: LFN write failed at LBA %d\n",
               (int)lfn_sector);
        kfree(sector);
        return -11;
      }
    }
  }

  disk_read_sector(entry_sector_lba, sector);
  struct fat_dir_entry *entries = (struct fat_dir_entry *)sector;
  struct fat_dir_entry *entry = &entries[entry_offset];
  if (entry) {
    if (!found_existing)
      memcpy(entry->name, short_name, 11);
    entry->attr = 0x20;
    entry->cluster_hi = (file_cluster >> 16);
    entry->cluster_low = (file_cluster & 0xFFFF);
    entry->size = size;
    if (disk_write_sector(entry_sector_lba, sector) != 0) {
      serial("[FAT] create_file: dir entry write failed at LBA %d\n",
             (int)entry_sector_lba);
      kfree(sector);
      return -11;
    }
  }

  kfree(sector);
  return 0;
}

extern "C" int fat32_create_file(const char *path, const void *data,
                                 uint32_t size) {
  return fat32_create_file_impl(path, data, size, 0, 0);
}

extern "C" int fat32_create_file_verified(const char *path, const void *data,
                                          uint32_t size, int verify) {
  return fat32_create_file_impl(path, data, size, verify ? 1 : 0, 0);
}

extern "C" int fat32_create_file_alloc(const char *path, uint32_t size) {
  return fat32_create_file_impl(path, NULL, size, 0, 1);
}

extern "C" int fat32_write_file_offset(const char *path, const void *data,
                                       uint32_t size, uint32_t offset,
                                       int verify) {
  if (!is_fat_initialized)
    return -1;
  if (!path || !data || size == 0)
    return -1;

  uint8_t *sector = (uint8_t *)kmalloc(512);
  if (!sector)
    return -2;

  if (disk_read_sector(current_lba, sector) != 0) {
    kfree(sector);
    return -3;
  }
  struct fat_bpb *bpb = (struct fat_bpb *)sector;
  if (bpb->bytes_per_sector == 0) {
    kfree(sector);
    return -4;
  }

  uint32_t fat_start = current_lba + bpb->reserved_sectors;
  uint32_t data_start = fat_start + (bpb->fats_count * bpb->sectors_per_fat_32);
  uint32_t root_cluster = bpb->root_cluster;
  uint8_t spc = bpb->sectors_per_cluster;
  uint16_t bps = bpb->bytes_per_sector;

  uint32_t parent_cluster;
  const char *fname;
  int fname_len;
  if (resolve_parent(path, root_cluster, data_start, fat_start, spc, bps,
                     &parent_cluster, &fname, &fname_len) != 0) {
    kfree(sector);
    return -5;
  }

  uint32_t file_cluster = 0;
  uint32_t entry_sector = 0;
  uint32_t entry_offset = 0;
  bool is_dir = false;
  if (find_in_cluster(parent_cluster, fname, fname_len, data_start, fat_start,
                      spc, bps, &file_cluster, NULL, &entry_sector,
                      &entry_offset, &is_dir) != 0) {
    kfree(sector);
    return -6;
  }
  if (is_dir || file_cluster == 0) {
    kfree(sector);
    return -7;
  }

  if (disk_read_sector(entry_sector, sector) != 0) {
    kfree(sector);
    return -7;
  }
  struct fat_dir_entry *entry = (struct fat_dir_entry *)sector;
  uint32_t existing_size = entry[entry_offset].size;

  uint32_t cluster_bytes = spc * bps;
  uint32_t skip_clusters = offset / cluster_bytes;
  uint32_t offset_in_cluster = offset % cluster_bytes;

  uint32_t alloc_hint = 2;
  uint32_t current_cluster = file_cluster;
  uint32_t prev_cluster = 0;
  for (uint32_t i = 0; i < skip_clusters; i++) {
    prev_cluster = current_cluster;
    current_cluster =
        fat_get_next_cluster(current_cluster, fat_start, bps, sector);
    if (current_cluster >= 0x0FFFFFF8) {
      uint32_t newc = fat_alloc_cluster_from(fat_start, bpb->sectors_per_fat_32,
                                             sector, &alloc_hint);
      if (newc == 0) {
        serial("[FAT] write_file_offset: alloc failed (skip) off=%d\n",
               (int)offset);
        kfree(sector);
        return -8;
      }
      fat_set_next_cluster(prev_cluster, newc, fat_start, bps, sector);
      current_cluster = newc;
    }
  }

  const uint8_t *p = (const uint8_t *)data;
  uint32_t remaining = size;
  uint32_t total_sectors = (size + 511) / 512;
  uint32_t sectors_done = 0;
  uint8_t *buf = (uint8_t *)kmalloc(512);
  uint8_t *vbuf = verify ? (uint8_t *)kmalloc(512) : NULL;
  if (!buf) {
    serial("[FAT] write_file_offset: OOM buf\n");
    kfree(sector);
    if (vbuf)
      kfree(vbuf);
    return -20;
  }
  while (remaining > 0 && current_cluster < 0x0FFFFFF8) {
    uint32_t cluster_lba = data_start + (current_cluster - 2) * spc;
    uint32_t sector_idx = offset_in_cluster / 512;
    uint32_t sector_off = offset_in_cluster % 512;
    for (; sector_idx < spc && remaining > 0; sector_idx++) {
      /* Fast path: contiguous full sectors, no verify */
      if (!verify && sector_off == 0) {
        uint32_t run = spc - sector_idx;
        uint32_t full = remaining / 512;
        if (full == 0) {
          /* fall through to partial sector handling */
        } else {
          if (run > full)
            run = full;
          if (disk_write_sectors(cluster_lba + sector_idx, run, p) != 0) {
            kfree(sector);
            kfree(buf);
            if (vbuf)
              kfree(vbuf);
            return -10;
          }
          p += run * 512;
          remaining -= run * 512;
          sectors_done += run;
          if (total_sectors > 2048 && (sectors_done % 256) == 0) {
            serial("[FAT] write_file_offset: wrote %d/%d sectors\n",
                   (int)sectors_done, (int)total_sectors);
          }
          sector_idx += (run - 1);
          continue;
        }
      }
      memset(buf, 0, 512);
      if (sector_off != 0 || remaining < 512) {
        if (disk_read_sector(cluster_lba + sector_idx, buf) != 0) {
          kfree(sector);
          kfree(buf);
          if (vbuf)
            kfree(vbuf);
          return -9;
        }
      }
      uint32_t copy = 512 - sector_off;
      if (copy > remaining)
        copy = remaining;
      memcpy(buf + sector_off, p, copy);
      int retries = verify ? 3 : 1;
      int ok = 0;
      for (int r = 0; r < retries; r++) {
        if (disk_write_sector(cluster_lba + sector_idx, buf) == 0) {
          if (!verify) {
            ok = 1;
            break;
          }
          if (vbuf && disk_read_sector(cluster_lba + sector_idx, vbuf) == 0 &&
              memcmp(buf, vbuf, 512) == 0) {
            ok = 1;
            break;
          }
        }
      }
      if (!ok) {
        kfree(sector);
        kfree(buf);
        if (vbuf)
          kfree(vbuf);
        return -10;
      }
      p += copy;
      remaining -= copy;
      sectors_done++;
      if (total_sectors > 2048 && (sectors_done % 256) == 0) {
        serial("[FAT] write_file_offset: wrote %d/%d sectors\n",
               (int)sectors_done, (int)total_sectors);
      }
      offset_in_cluster = 0;
      sector_off = 0;
    }
    if (remaining > 0) {
      uint32_t next =
          fat_get_next_cluster(current_cluster, fat_start, bps, sector);
      if (next >= 0x0FFFFFF8) {
        uint32_t newc = fat_alloc_cluster_from(
            fat_start, bpb->sectors_per_fat_32, sector, &alloc_hint);
        if (newc == 0) {
          serial("[FAT] write_file_offset: alloc failed (extend) off=%d\n",
                 (int)(offset + (size - remaining)));
          kfree(sector);
          kfree(buf);
          if (vbuf)
            kfree(vbuf);
          return -8;
        }
        fat_set_next_cluster(current_cluster, newc, fat_start, bps, sector);
        next = newc;
      }
      current_cluster = next;
    }
  }
  if (remaining != 0) {
    kfree(sector);
    kfree(buf);
    if (vbuf)
      kfree(vbuf);
    return -11;
  }

  uint32_t new_size = offset + size;
  if (new_size > existing_size) {
    if (disk_read_sector(entry_sector, sector) != 0) {
      kfree(sector);
      kfree(buf);
      if (vbuf)
        kfree(vbuf);
      return -12;
    }
    struct fat_dir_entry *ent = (struct fat_dir_entry *)sector;
    ent[entry_offset].size = new_size;
    if (disk_write_sector(entry_sector, sector) != 0) {
      kfree(sector);
      kfree(buf);
      if (vbuf)
        kfree(vbuf);
      return -12;
    }
  }
  kfree(buf);
  if (vbuf)
    kfree(vbuf);
  kfree(sector);
  return 0;
}

extern "C" void fat32_list_directory(const char *path) {
  if (!is_fat_initialized) {
    terminal_writestring("FAT not mounted.\n");
    return;
  }

  uint8_t *sector = (uint8_t *)kmalloc(512);
  if (!sector)
    return;

  /* 1. Read BPB */
  disk_read_sector(current_lba, sector);
  struct fat_bpb *bpb = (struct fat_bpb *)sector;

  if (bpb->bytes_per_sector == 0) {
    terminal_writestring("Error: Invalid FAT32 BPB (bytes_per_sector=0).\n");
    kfree(sector);
    return;
  }

  uint32_t fat_start = current_lba + bpb->reserved_sectors;
  uint32_t data_start = fat_start + (bpb->fats_count * bpb->sectors_per_fat_32);
  uint32_t root_cluster = bpb->root_cluster;
  uint8_t spc = bpb->sectors_per_cluster;
  uint16_t bps = bpb->bytes_per_sector;

  uint32_t target_cluster = 0;
  const char *display_path = "/";

  /* Resolve path if not root */
  if (path && path[0] && !(path[0] == '/' && path[1] == 0)) {
    display_path = path;

    uint32_t parent_cluster;
    const char *fname;
    int fname_len;
    if (resolve_parent(path, root_cluster, data_start, fat_start, spc, bps,
                       &parent_cluster, &fname, &fname_len) != 0) {
      terminal_printf("Path not found: %s\n", path);
      kfree(sector);
      return;
    }

    bool is_dir;
    if (find_in_cluster(parent_cluster, fname, fname_len, data_start, fat_start,
                        spc, bps, &target_cluster, NULL, NULL, NULL,
                        &is_dir) != 0) {
      terminal_printf("Directory not found: %s\n", path);
      kfree(sector);
      return;
    }
    if (!is_dir) {
      terminal_printf("Not a directory: %s\n", path);
      kfree(sector);
      return;
    }
  } else {
    target_cluster = root_cluster;
  }

  if (target_cluster == 0)
    target_cluster = root_cluster;

  /* List contents of target_cluster */
  terminal_printf("Listing %s:\n", display_path);

  uint32_t current_cluster = target_cluster;

  while (true) {
    uint32_t cluster_lba = data_start + (current_cluster - 2) * spc;

    char lfn_buf[260];
    bool lfn_active = false;
    uint8_t lfn_chk = 0;
    lfn_reset(lfn_buf);
    for (int i = 0; i < spc; i++) {
      disk_read_sector(cluster_lba + i, sector);
      struct fat_dir_entry *entries = (struct fat_dir_entry *)sector;

      for (int j = 0; j < 512 / 32; j++) {
        if (entries[j].name[0] == 0)
          goto done_listing;
        if ((uint8_t)entries[j].name[0] == 0xE5)
          continue;
        if (entries[j].attr == 0x0F) {
          struct fat_lfn_entry *lfn = (struct fat_lfn_entry *)&entries[j];
          int seq = lfn->seq & 0x1F;
          if (lfn->seq & 0x40) {
            lfn_reset(lfn_buf);
            lfn_active = true;
            lfn_chk = lfn->checksum;
          }
          if (lfn_active) {
            lfn_put_part(lfn_buf, sizeof(lfn_buf), seq, lfn);
          }
          continue;
        }
        const char *display_name = 0;
        if (lfn_active) {
          lfn_finalize(lfn_buf, sizeof(lfn_buf));
          if (lfn_chk == lfn_checksum((const char *)entries[j].name)) {
            display_name = lfn_buf;
          }
          lfn_active = false;
        }

        char name[13];
        int k = 0;
        for (int m = 0; m < 8; m++) {
          if (entries[j].name[m] != ' ')
            name[k++] = entries[j].name[m];
        }
        if (entries[j].name[8] != ' ') {
          name[k++] = '.';
          for (int m = 8; m < 11; m++) {
            if (entries[j].name[m] != ' ')
              name[k++] = entries[j].name[m];
          }
        }
        name[k] = 0;

        if (!display_name)
          display_name = name;

        if (entries[j].attr & 0x10) {
          terminal_printf("  [DIR]  %s\n", display_name);
        } else {
          terminal_printf("  [FILE] %s  (%u bytes)\n", display_name,
                          entries[j].size);
        }
      }
    }

    /* Next cluster */
    current_cluster =
        fat_get_next_cluster(current_cluster, fat_start, bps, sector);
    if (current_cluster >= 0x0FFFFFF8)
      break;
  }

done_listing:
  kfree(sector);
}

extern "C" int fat32_read_directory(const char *path, fat_file_info_t *out,
                                    int max_entries) {
  if (!is_fat_initialized)
    return 0;

  uint8_t *sector = (uint8_t *)kmalloc(512);
  if (!sector)
    return 0;

  /* 1. Read BPB */
  disk_read_sector(current_lba, sector);
  struct fat_bpb *bpb = (struct fat_bpb *)sector;

  uint32_t fat_start = current_lba + bpb->reserved_sectors;
  uint32_t data_start = fat_start + (bpb->fats_count * bpb->sectors_per_fat_32);
  uint32_t root_cluster = bpb->root_cluster;
  uint8_t spc = bpb->sectors_per_cluster;
  uint16_t bps = bpb->bytes_per_sector;

  uint32_t target_cluster = 0;

  /* Resolve path (simplified) */
  if (path && path[0] && !(path[0] == '/' && path[1] == 0)) {
    uint32_t parent_cluster;
    const char *fname;
    int fname_len;
    if (resolve_parent(path, root_cluster, data_start, fat_start, spc, bps,
                       &parent_cluster, &fname, &fname_len) != 0) {
      kfree(sector);
      return 0;
    }
    bool is_dir;
    if (find_in_cluster(parent_cluster, fname, fname_len, data_start, fat_start,
                        spc, bps, &target_cluster, NULL, NULL, NULL,
                        &is_dir) != 0) {
      kfree(sector);
      return 0;
    }
    if (!is_dir) {
      kfree(sector);
      return 0;
    }
  } else {
    target_cluster = root_cluster;
  }

  if (target_cluster == 0)
    target_cluster = root_cluster;

  int count = 0;
  uint32_t current_cluster = target_cluster;

  while (count < max_entries) {
    uint32_t cluster_lba = data_start + (current_cluster - 2) * spc;
    char lfn_buf[260];
    bool lfn_active = false;
    uint8_t lfn_chk = 0;
    lfn_reset(lfn_buf);

    for (int i = 0; i < spc && count < max_entries; i++) {
      disk_read_sector(cluster_lba + i, sector);
      struct fat_dir_entry *entries = (struct fat_dir_entry *)sector;

      for (int j = 0; j < 512 / 32 && count < max_entries; j++) {
        if (entries[j].name[0] == 0)
          goto done_reading;
        if ((uint8_t)entries[j].name[0] == 0xE5)
          continue;
        if (entries[j].attr == 0x0F) {
          struct fat_lfn_entry *lfn = (struct fat_lfn_entry *)&entries[j];
          int seq = lfn->seq & 0x1F;
          if (lfn->seq & 0x40) {
            lfn_reset(lfn_buf);
            lfn_active = true;
            lfn_chk = lfn->checksum;
          }
          if (lfn_active) {
            lfn_put_part(lfn_buf, sizeof(lfn_buf), seq, lfn);
          }
          continue;
        }
        const char *display_name = 0;
        if (lfn_active) {
          lfn_finalize(lfn_buf, sizeof(lfn_buf));
          if (lfn_chk == lfn_checksum((const char *)entries[j].name)) {
            display_name = lfn_buf;
          }
          lfn_active = false;
        }

        /* Format Name */
        char name[13];
        int k = 0;
        for (int m = 0; m < 8; m++) {
          if (entries[j].name[m] != ' ')
            name[k++] = entries[j].name[m];
        }
        if (entries[j].name[8] != ' ') {
          name[k++] = '.';
          for (int m = 8; m < 11; m++) {
            if (entries[j].name[m] != ' ')
              name[k++] = entries[j].name[m];
          }
        }
        name[k] = 0;

        if (!display_name)
          display_name = name;
        strncpy(out[count].name, display_name, sizeof(out[count].name) - 1);
        out[count].name[sizeof(out[count].name) - 1] = 0;
        out[count].size = entries[j].size;
        out[count].is_dir = (entries[j].attr & 0x10) ? 1 : 0;
        count++;
      }
    }

    current_cluster =
        fat_get_next_cluster(current_cluster, fat_start, bps, sector);
    if (current_cluster >= 0x0FFFFFF8)
      break;
  }
done_reading:
  kfree(sector);
  return count;
}

extern "C" int fat32_delete_file(const char *path) {
  if (!is_fat_initialized)
    return -1;

  uint8_t *sector = (uint8_t *)kmalloc(512);
  if (!sector)
    return -1;

  disk_read_sector(current_lba, sector);
  struct fat_bpb *bpb = (struct fat_bpb *)sector;

  if (bpb->bytes_per_sector == 0) {
    terminal_writestring("Error: Invalid FAT32 BPB (bytes_per_sector=0).\n");
    kfree(sector);
    return -1;
  }

  uint32_t fat_start = current_lba + bpb->reserved_sectors;
  uint32_t data_start = fat_start + (bpb->fats_count * bpb->sectors_per_fat_32);
  uint32_t root_cluster = bpb->root_cluster;
  uint8_t spc = bpb->sectors_per_cluster;
  uint16_t bps = bpb->bytes_per_sector;

  uint32_t parent_cluster;
  const char *fname;
  int fname_len;
  if (resolve_parent(path, root_cluster, data_start, fat_start, spc, bps,
                     &parent_cluster, &fname, &fname_len) != 0) {
    kfree(sector);
    return -1;
  }

  uint32_t file_cluster = 0;
  uint32_t entry_sector;
  uint32_t entry_offset;
  bool is_dir;
  if (find_in_cluster(parent_cluster, fname, fname_len, data_start, fat_start,
                      spc, bps, &file_cluster, NULL, &entry_sector,
                      &entry_offset, &is_dir) != 0) {
    kfree(sector);
    return -1;
  }

  /* Mark deleted in directory entry */
  disk_read_sector(entry_sector, sector);
  ((struct fat_dir_entry *)sector)[entry_offset].name[0] = 0xE5;
  disk_write_sector(entry_sector, sector);

  /* Delete preceding LFN entries */
  int entry_idx =
      dir_calc_entry_index(parent_cluster, entry_sector, entry_offset,
                           data_start, fat_start, spc, bps);
  dir_mark_lfn_deleted(parent_cluster, entry_idx, data_start, fat_start, spc,
                       bps);

  /* Free cluster chain */
  if (file_cluster != 0) {
    uint32_t current = file_cluster;
    while (current < 0x0FFFFFF8 && current != 0) {
      uint32_t fat_sector = fat_start + (current * 4) / bps;
      uint32_t fat_offset = (current * 4) % bps;

      disk_read_sector(fat_sector, sector);
      uint32_t next = (*(uint32_t *)(sector + fat_offset)) & 0x0FFFFFFF;

      /* Mark free */
      *(uint32_t *)(sector + fat_offset) = 0;
      disk_write_sector(fat_sector, sector);

      current = next;
    }
  }

  kfree(sector);
  return 0;
}

extern "C" void fat32_deduplicate_root() {
  if (!is_fat_initialized)
    return;

  uint8_t *sector = (uint8_t *)kmalloc(512);
  if (!sector)
    return;

  /* 1. Read BPB to get root cluster etc */
  if (disk_read_sector(current_lba, sector) != 0) {
    kfree(sector);
    return;
  }
  struct fat_bpb *bpb = (struct fat_bpb *)sector;
  uint32_t fat_start = current_lba + bpb->reserved_sectors;
  uint32_t data_start = fat_start + (bpb->fats_count * bpb->sectors_per_fat_32);
  uint32_t root_cluster = bpb->root_cluster;
  uint8_t spc = bpb->sectors_per_cluster;
  uint16_t bps = bpb->bytes_per_sector;

  const char *targets[] = {"BOOT       ", "SYSTEM     "};
  bool found[] = {false, false};

  uint32_t curr = root_cluster;
  while (curr < 0x0FFFFFF8 && curr >= 2) {
    uint32_t cluster_lba = data_start + (curr - 2) * spc;
    for (int i = 0; i < spc; i++) {
      if (disk_read_sector(cluster_lba + i, sector) != 0)
        break;
      struct fat_dir_entry *entries = (struct fat_dir_entry *)sector;
      bool modified = false;
      for (int j = 0; j < 512 / 32; j++) {
        if (entries[j].name[0] == 0)
          break;
        if ((uint8_t)entries[j].name[0] == 0xE5)
          continue;
        if (entries[j].attr == 0x0F)
          continue;

        for (int t = 0; t < 2; t++) {
          if (memcmp(entries[j].name, targets[t], 11) == 0) {
            if (found[t]) {
              /* DUPLICATE! Kill it. */
              entries[j].name[0] = 0xE5;
              modified = true;
              serial(
                  "[FAT] deduplicate: removing surplus %s entry at index %d\n",
                  targets[t], j);
            } else {
              found[t] = true;
            }
          }
        }
      }
      if (modified) {
        disk_write_sector(cluster_lba + i, sector);
      }
    }
    curr = fat_get_next_cluster(curr, fat_start, bps, sector);
  }
  kfree(sector);
}

static int write_sector_verified(uint32_t lba, const uint8_t *buf, int verify) {
  int local_verify = verify;
  uint8_t *verify_buf = 0;
  if (local_verify) {
    verify_buf = (uint8_t *)kmalloc(512);
    if (!verify_buf) {
      serial("[FAT] verify buffer alloc failed; fallback non-verify\n");
      local_verify = 0;
    }
  }
  int retries = local_verify ? 3 : 1;
  for (int r = 0; r < retries; r++) {
    if (disk_write_sector(lba, buf) != 0) {
      serial("[FAT] mkdir: write failed LBA %d\n", (int)lba);
    } else if (!local_verify) {
      if (verify_buf)
        kfree(verify_buf);
      return 0;
    } else {
      if (disk_read_sector(lba, verify_buf) == 0 &&
          memcmp(buf, verify_buf, 512) == 0) {
        if (verify_buf)
          kfree(verify_buf);
        return 0;
      }
      serial("[FAT] mkdir: verify failed LBA %d\n", (int)lba);
    }
  }
  if (verify_buf)
    kfree(verify_buf);
  return -1;
}

static int fat32_create_directory_impl(const char *path, int verify) {
  if (!path) {
    serial("[FAT] mkdir: invalid path (null)\n");
    return -101;
  }
  uintptr_t pval = (uintptr_t)path;
  if (pval < 0x1000) {
    serial("[FAT] mkdir: invalid path pointer 0x%x\n", (unsigned)pval);
    return -101;
  }
  if (path[0] == 0) {
    serial("[FAT] mkdir: empty path @0x%x\n", (unsigned)pval);
    return -101;
  }
  if (path[0] == '/' && path[1] == 0) {
    serial("[FAT] mkdir: root path not allowed @0x%x\n", (unsigned)pval);
    return -101;
  }

  uint8_t *sector = (uint8_t *)kmalloc(512);
  if (!sector) {
    serial("[FAT] mkdir: no memory for sector buffer\n");
    return -102;
  }

  if (disk_read_sector(current_lba, sector) != 0) {
    serial("[FAT] mkdir: BPB read failed LBA %d\n", (int)current_lba);
    kfree(sector);
    return -103;
  }
  struct fat_bpb *bpb = (struct fat_bpb *)sector;
  if (bpb->bytes_per_sector == 0) {
    serial("[FAT] mkdir: invalid BPB (bytes_per_sector=0)\n");
    kfree(sector);
    return -104;
  }

  uint32_t fat_start = current_lba + bpb->reserved_sectors;
  uint32_t data_start = fat_start + (bpb->fats_count * bpb->sectors_per_fat_32);
  uint32_t root_cluster = bpb->root_cluster;
  uint8_t spc = bpb->sectors_per_cluster;
  uint16_t bps = bpb->bytes_per_sector;
  uint32_t sectors_per_fat = bpb->sectors_per_fat_32;

  uint32_t parent_cluster = 0;
  const char *fname = 0;
  int fname_len = 0;
  if (resolve_parent(path, root_cluster, data_start, fat_start, spc, bps,
                     &parent_cluster, &fname, &fname_len) != 0) {
    /* fallback for "/name" */
    if (path[0] == '/' && path[1] != 0 && strchr(path + 1, '/') == 0) {
      serial("[FAT] mkdir: resolve_parent failed, using root fallback for %s\n",
             path);
      parent_cluster = root_cluster;
      fname = path + 1;
      fname_len = (int)strlen(fname);
    } else {
      serial("[FAT] mkdir: resolve_parent failed for %s\n", path);
      kfree(sector);
      return -105;
    }
  }
  if (fname_len <= 0 || fname_len > 255) {
    serial("[FAT] mkdir: invalid name length %d for %s\n", fname_len, path);
    kfree(sector);
    return -106;
  }

  /* Check if it already exists */
  uint32_t existing_cluster = 0;
  bool existing_is_dir = false;
  if (find_in_cluster(parent_cluster, fname, fname_len, data_start, fat_start,
                      spc, bps, &existing_cluster, NULL, NULL, NULL,
                      &existing_is_dir) == 0) {
    if (existing_is_dir) {
      serial("[FAT] mkdir: %s already exists (directory), ok\n", path);
      kfree(sector);
      return 0; // Success, already exists
    } else {
      serial("[FAT] mkdir: %s already exists as a file!\n", path);
      kfree(sector);
      return -109; // Error, exists as file
    }
  }

  /* allocate cluster */
  uint32_t dir_cluster = fat_alloc_cluster(fat_start, sectors_per_fat, sector);
  if (dir_cluster == 0) {
    serial("[FAT] mkdir: alloc cluster failed for %s\n", path);
    kfree(sector);
    return -107;
  }
  if (fat_set_next_cluster_checked(dir_cluster, 0x0FFFFFFF, fat_start, bps,
                                   sector, verify) != 0) {
    serial("[FAT] mkdir: set EOC failed for cluster %u\n",
           (unsigned)dir_cluster);
    kfree(sector);
    return -108;
  }

  /* create directory entry in parent */
  char name_buf[256];
  copy_component(name_buf, sizeof(name_buf), fname, fname_len);
  int dc = dir_create_entry_with_cluster(
      parent_cluster, name_buf, fname_len, dir_cluster, 0, 0x10, data_start,
      fat_start, spc, bps, sectors_per_fat, verify);
  if (dc != 0) {
    serial("[FAT] mkdir: entry creation failed (code=%d)\n", dc);
    if (fat_set_next_cluster_checked(dir_cluster, 0, fat_start, bps, sector,
                                     verify) != 0) {
      serial("[FAT] mkdir: failed to free cluster %u\n", (unsigned)dir_cluster);
    }
    kfree(sector);
    return dc;
  }

  /* initialize directory cluster */
  uint32_t cluster_lba = data_start + (dir_cluster - 2) * spc;
  memset(sector, 0, 512);
  struct fat_dir_entry *dot = (struct fat_dir_entry *)sector;
  memset(dot[0].name, ' ', 11);
  dot[0].name[0] = '.';
  dot[0].attr = 0x10;
  dot[0].cluster_hi = (dir_cluster >> 16);
  dot[0].cluster_low = (dir_cluster & 0xFFFF);

  memset(dot[1].name, ' ', 11);
  dot[1].name[0] = '.';
  dot[1].name[1] = '.';
  dot[1].attr = 0x10;
  uint32_t parent_for_dotdot = parent_cluster;
  if (parent_for_dotdot == root_cluster)
    parent_for_dotdot = 0;
  dot[1].cluster_hi = (parent_for_dotdot >> 16);
  dot[1].cluster_low = (parent_for_dotdot & 0xFFFF);

  if (write_sector_verified(cluster_lba, sector, verify) != 0) {
    serial("[FAT] mkdir: dot sector write failed LBA %d\n", (int)cluster_lba);
    kfree(sector);
    return -12;
  }

  memset(sector, 0, 512);
  for (int i = 1; i < spc; i++) {
    if (write_sector_verified(cluster_lba + i, sector, verify) != 0) {
      serial("[FAT] mkdir: clear sector write failed LBA %d\n",
             (int)(cluster_lba + i));
      kfree(sector);
      return -13;
    }
  }

  kfree(sector);
  return 0;
}

extern "C" int fat32_create_directory(const char *path) {
  return fat32_create_directory_impl(path, 0);
}

extern "C" int fat32_create_directory_verified(const char *path, int verify) {
  return fat32_create_directory_impl(path, verify ? 1 : 0);
}

extern "C" int fat32_directory_exists(const char *path) {
  if (!is_fat_initialized)
    return 0;

  /* Root always exists */
  if (strcmp(path, "/") == 0 || strcmp(path, "") == 0)
    return 1;

  uint8_t *sector = (uint8_t *)kmalloc(512);
  if (!sector)
    return 0;

  disk_read_sector(current_lba, sector);
  struct fat_bpb *bpb = (struct fat_bpb *)sector;

  if (bpb->bytes_per_sector == 0) {
    // Invalid BPB, assume directory doesn't exist
    kfree(sector);
    return 0;
  }

  uint32_t fat_start = current_lba + bpb->reserved_sectors;
  uint32_t data_start = fat_start + (bpb->fats_count * bpb->sectors_per_fat_32);
  uint32_t root_cluster = bpb->root_cluster;
  uint8_t spc = bpb->sectors_per_cluster;

  uint32_t parent_cluster;
  const char *fname;
  int fname_len;
  if (resolve_parent(path, root_cluster, data_start, fat_start, spc,
                     bpb->bytes_per_sector, &parent_cluster, &fname,
                     &fname_len) != 0) {
    kfree(sector);
    return 0;
  }

  bool is_dir;
  int res = find_in_cluster(parent_cluster, fname, fname_len, data_start,
                            fat_start, spc, bpb->bytes_per_sector, NULL, NULL,
                            NULL, NULL, &is_dir);

  kfree(sector);
  return (res == 0 && is_dir) ? 1 : 0;
}

extern "C" int fat32_rename(const char *src, const char *dst) {
  if (!is_fat_initialized)
    return -1;

  uint8_t *sector = (uint8_t *)kmalloc(512);
  if (!sector)
    return -1;

  disk_read_sector(current_lba, sector);
  struct fat_bpb *bpb = (struct fat_bpb *)sector;
  if (bpb->bytes_per_sector == 0) {
    kfree(sector);
    return -1;
  }

  uint32_t fat_start = current_lba + bpb->reserved_sectors;
  uint32_t data_start = fat_start + (bpb->fats_count * bpb->sectors_per_fat_32);
  uint32_t root_cluster = bpb->root_cluster;
  uint8_t spc = bpb->sectors_per_cluster;
  uint16_t bps = bpb->bytes_per_sector;
  uint32_t sectors_per_fat = bpb->sectors_per_fat_32;

  uint32_t src_parent;
  const char *src_name;
  int src_len;
  if (resolve_parent(src, root_cluster, data_start, fat_start, spc, bps,
                     &src_parent, &src_name, &src_len) != 0) {
    kfree(sector);
    return -1;
  }

  uint32_t dst_parent;
  const char *dst_name;
  int dst_len;
  if (resolve_parent(dst, root_cluster, data_start, fat_start, spc, bps,
                     &dst_parent, &dst_name, &dst_len) != 0) {
    kfree(sector);
    return -1;
  }

  if (dst_len <= 0 || dst_len > 255) {
    kfree(sector);
    return -1;
  }

  uint32_t src_cluster = 0;
  uint32_t src_size = 0;
  uint32_t src_sector = 0;
  uint32_t src_offset = 0;
  bool is_dir = false;
  if (find_in_cluster(src_parent, src_name, src_len, data_start, fat_start, spc,
                      bps, &src_cluster, &src_size, &src_sector, &src_offset,
                      &is_dir) != 0) {
    kfree(sector);
    return -1;
  }

  if (find_in_cluster(dst_parent, dst_name, dst_len, data_start, fat_start, spc,
                      bps, NULL, NULL, NULL, NULL, NULL) == 0) {
    kfree(sector);
    return -2; /* destination exists */
  }

  char dst_buf[256];
  copy_component(dst_buf, sizeof(dst_buf), dst_name, dst_len);
  uint8_t attr = is_dir ? 0x10 : 0x20;

  if (dir_create_entry_with_cluster(dst_parent, dst_buf, dst_len, src_cluster,
                                    src_size, attr, data_start, fat_start, spc,
                                    bps, sectors_per_fat, 0) != 0) {
    kfree(sector);
    return -1;
  }

  /* If directory moved, update .. entry to new parent */
  if (is_dir && src_cluster != 0) {
    uint32_t dir_lba = data_start + (src_cluster - 2) * spc;
    disk_read_sector(dir_lba, sector);
    struct fat_dir_entry *entries = (struct fat_dir_entry *)sector;
    if (entries[1].name[0] == '.' && entries[1].name[1] == '.') {
      uint32_t parent_for_dotdot = dst_parent;
      if (parent_for_dotdot == root_cluster)
        parent_for_dotdot = 0;
      entries[1].cluster_hi = (parent_for_dotdot >> 16);
      entries[1].cluster_low = (parent_for_dotdot & 0xFFFF);
      disk_write_sector(dir_lba, sector);
    }
  }

  /* delete old entry + LFN, but keep clusters */
  disk_read_sector(src_sector, sector);
  ((struct fat_dir_entry *)sector)[src_offset].name[0] = 0xE5;
  disk_write_sector(src_sector, sector);

  int entry_idx = dir_calc_entry_index(src_parent, src_sector, src_offset,
                                       data_start, fat_start, spc, bps);
  dir_mark_lfn_deleted(src_parent, entry_idx, data_start, fat_start, spc, bps);

  kfree(sector);
  return 0;
}

extern "C" int fat32_format(uint32_t lba, uint32_t sector_count,
                            const char *label) {
  if (sector_count < 65536) {
    terminal_writestring(
        "Error: Partition too small for FAT32 (need > 32MB approx)\n");
    return -1;
  }

  uint8_t *sector = (uint8_t *)kmalloc_aligned(512, 16);
  if (!sector)
    return -1;
  memset(sector, 0, 512);

  /* 1. Setup BPB */
  struct fat_bpb *bpb = (struct fat_bpb *)sector;

  /* Jump Boot Code */
  bpb->jmp[0] = 0xEB;
  bpb->jmp[1] = 0x58;
  bpb->jmp[2] = 0x90;
  memcpy(bpb->oem, "MSWIN4.1", 8);

  bpb->bytes_per_sector = 512;
  /* Use larger clusters to reduce FAT chain size for large files */
  bpb->sectors_per_cluster = 128; // 64KB clusters
  bpb->reserved_sectors = 32;
  bpb->fats_count = 2;
  bpb->root_entries_count = 0; // FAT32
  bpb->total_sectors_16 = 0;
  bpb->media_descriptor = 0xF8;
  bpb->sectors_per_fat_16 = 0;
  bpb->sectors_per_track = 32; // Dummy
  bpb->heads_count = 64;       // Dummy
  bpb->hidden_sectors = lba;
  bpb->total_sectors_32 = sector_count;

  /* Calculate FAT size */
  /* Formula: FatSectors = (Total - Res) / (128 * SPC + 2) */
  uint32_t data_sectors = sector_count - bpb->reserved_sectors;
  uint32_t divisor = (128 * bpb->sectors_per_cluster) + 2;
  uint32_t fat_sectors = (data_sectors + divisor - 1) / divisor;

  bpb->sectors_per_fat_32 = fat_sectors;

  /* Save parameters before clearing buffer for next steps */
  uint32_t reserved_sectors = bpb->reserved_sectors;
  uint32_t fats_count = bpb->fats_count;
  uint32_t sectors_per_cluster = bpb->sectors_per_cluster;

  bpb->ext_flags = 0;
  bpb->fs_version = 0;
  bpb->root_cluster = 2;
  bpb->fs_info = 1;
  bpb->backup_boot_sector = 6;
  bpb->drive_number = 0x80;
  bpb->boot_signature = 0x29;
  bpb->volume_id = 0x12345678;

  char vol_label[11];
  memset(vol_label, ' ', 11);
  if (label) {
    for (int i = 0; i < 11 && label[i]; i++)
      vol_label[i] = label[i];
  } else {
    memcpy(vol_label, "NO NAME    ", 11);
  }
  memcpy(bpb->volume_label, vol_label, 11);
  memcpy(bpb->fs_type, "FAT32   ", 8);

  sector[510] = 0x55;
  sector[511] = 0xAA;

  /* Write Boot Sector (LBA 0) */
  if (disk_write_sector(lba, sector) != 0) {
    terminal_writestring("Error: Failed to write Boot Sector\n");
    kfree(sector);
    return -1;
  }

  /* Write Backup Boot Sector (LBA 6) */
  if (disk_write_sector(lba + 6, sector) != 0) {
    serial("[FAT] Warning: Failed to write Backup Boot Sector\n");
  }

  /* 2. Setup FSInfo (LBA 1) */
  memset(sector, 0, 512);
  struct fat_fsinfo *fsinfo = (struct fat_fsinfo *)sector;
  fsinfo->lead_sig = 0x41615252;
  fsinfo->struc_sig = 0x61417272;
  fsinfo->free_count = 0xFFFFFFFF;
  fsinfo->next_free = 0xFFFFFFFF;
  fsinfo->trail_sig = 0xAA550000;

  if (disk_write_sector(lba + 1, sector) != 0)
    serial("[FAT] Warning: Failed to write FSInfo\n");
  disk_write_sector(lba + 7, sector);

  /* 3. Initialize FATs */
  /* We only need to init the first sector of each FAT */
  memset(sector, 0, 512);
  uint32_t *fat_table = (uint32_t *)sector;
  fat_table[0] = 0x0FFFFFF8; // Media
  fat_table[1] = 0x0FFFFFFF; // EOC
  fat_table[2] = 0x0FFFFFFF; // EOC for Root Dir

  uint32_t fat1_lba = lba + reserved_sectors;
  uint32_t fat2_lba = fat1_lba + fat_sectors;

  if (disk_write_sector(fat1_lba, sector) != 0)
    serial("[FAT] Warning: Failed to write FAT1\n");
  disk_write_sector(fat2_lba, sector);

  /* Clear remaining FAT sectors so free clusters are actually zeroed */
  memset(sector, 0, 512);
  for (uint32_t s = 1; s < fat_sectors; s++) {
    disk_write_sector(fat1_lba + s, sector);
  }
  for (uint32_t s = 1; s < fat_sectors; s++) {
    disk_write_sector(fat2_lba + s, sector);
  }

  /* 4. Initialize Root Directory (Cluster 2) */
  /* Data Start = Res + Fats * FatSec */
  uint32_t data_start = lba + reserved_sectors + (fats_count * fat_sectors);
  /* Cluster 2 is at offset 0 from data_start */
  memset(sector, 0, 512);
  for (uint32_t i = 0; i < sectors_per_cluster; i++) {
    if (disk_write_sector(data_start + i, sector) != 0)
      serial("[FAT] Warning: Failed to write RootDir sector %d\n", i);
  }

  kfree(sector);
  return 0;
}

extern "C" int32_t fat32_get_file_size(const char *path) {
  if (!is_fat_initialized)
    return -1;

  uint8_t *sector = (uint8_t *)kmalloc(512);
  if (!sector)
    return -1;

  /* 1. Read BPB */
  if (disk_read_sector(current_lba, sector) != 0) {
    kfree(sector);
    return -1;
  }
  struct fat_bpb *bpb = (struct fat_bpb *)sector;

  if (bpb->bytes_per_sector == 0) {
    kfree(sector);
    return -1;
  }

  uint32_t fat_start = current_lba + bpb->reserved_sectors;
  uint32_t data_start = fat_start + (bpb->fats_count * bpb->sectors_per_fat_32);
  uint32_t root_cluster = bpb->root_cluster;
  uint8_t spc = bpb->sectors_per_cluster;

  /* 2. Resolve Path */
  uint32_t parent_cluster;
  const char *fname;
  int fname_len;
  if (resolve_parent(path, root_cluster, data_start, fat_start, spc,
                     bpb->bytes_per_sector, &parent_cluster, &fname,
                     &fname_len) != 0) {
    kfree(sector);
    return -1;
  }

  uint32_t file_size = 0;
  bool is_dir;
  int res = find_in_cluster(parent_cluster, fname, fname_len, data_start,
                            fat_start, spc, bpb->bytes_per_sector, NULL,
                            &file_size, NULL, NULL, &is_dir);

  kfree(sector);
  return (res == 0 && !is_dir) ? (int32_t)file_size : -1;
}

/* Încearcă să monteze automat prima partiție FAT găsită */
void fat_automount(void) {
  if (is_fat_initialized)
    return;

  /* 1. Verificăm dacă avem partiții detectate. Dacă nu, scanăm. */
  bool has_partitions = false;
  for (int i = 0; i < 26; i++) {
    if (g_assigns[i].used) {
      has_partitions = true;
      break;
    }
  }

  if (!has_partitions) {
    // terminal_writestring("[AutoMount] Probing partitions...\n");
    disk_probe_partitions();
  }

  /* 2. Căutăm prima partiție de tip FAT32 (0x0B sau 0x0C) */
  for (int i = 0; i < 26; i++) {
    if (g_assigns[i].used) {
      uint8_t t = g_assigns[i].type;
      if (t == 0x0B || t == 0x0C) {
        /* Check for valid boot signature before attempting mount */
        uint8_t *check_buf = (uint8_t *)kmalloc(512);
        if (check_buf) {
          disk_read_sector(g_assigns[i].lba, check_buf);
          if (check_buf[510] != 0x55 || check_buf[511] != 0xAA) {
            kfree(check_buf);
            continue; /* Skip unformatted partition */
          }
          kfree(check_buf);
        }

        terminal_printf(
            "[AutoMount] Mounting FAT32 on partition %c (LBA %u)...\n",
            g_assigns[i].letter, g_assigns[i].lba);
        if (fat32_init(0, g_assigns[i].lba) == 0) {
          is_fat_initialized = true;
          current_lba = g_assigns[i].lba;
          current_letter = g_assigns[i].letter;
          return;
        }
      }
    }
  }
}

static void cmd_usage(void) {
  terminal_writestring("Usage: fat <command>\n");
  terminal_writestring("Commands:\n");
  terminal_writestring(
      "  mount <part>   Mount FAT32 on partition letter (e.g. 'a')\n");
  terminal_writestring("  ls             List root directory\n");
  terminal_writestring("  info           Show filesystem info\n");
}

extern "C" int cmd_fat(int argc, char **argv) {
  if (argc < 2) {
    cmd_usage();
    return -1;
  }

  const char *sub = argv[1];

  if (strcmp(sub, "ls") == 0) {
    fat_automount();
    if (is_fat_initialized) {
      fat32_list_directory("/");
    } else
      terminal_writestring("FAT not mounted (no FAT32 partition found).\n");
    return 0;
  }

  if (strcmp(sub, "info") == 0) {
    fat_automount();
    if (is_fat_initialized) {
      terminal_writestring("FAT32 Filesystem Status:\n");
      terminal_writestring("  State: Mounted\n");
      terminal_printf("  Partition: %c\n",
                      current_letter ? current_letter : '?');
      terminal_printf("  LBA Start: %u\n", current_lba);
    } else
      terminal_writestring("FAT not mounted.\n");
    return 0;
  }

  if (strcmp(sub, "mount") == 0) {
    if (argc < 3) {
      terminal_writestring("Usage: fat mount <partition_letter>\n");
      return -1;
    }

    char letter = argv[2][0];
    if (letter >= 'A' && letter <= 'Z')
      letter += 32; // to lowercase

    int idx = letter - 'a';
    if (idx < 0 || idx >= 26) {
      terminal_writestring("Invalid partition letter.\n");
      return -1;
    }

    if (!g_assigns[idx].used) {
      terminal_printf(
          "Partition '%c' is not assigned. Run 'disk probe' first.\n", letter);
      return -1;
    }

    uint32_t lba = g_assigns[idx].lba;
    terminal_printf("Mounting FAT32 on partition %c (LBA %u)...\n", letter,
                    lba);

    // 0 = device ID (ignorat dacă fat.c folosește disk_read_sector global)
    if (fat32_init(0, lba) == 0) {
      terminal_writestring("Mount successful.\n");
      is_fat_initialized = true;
      current_lba = lba;
      current_letter = letter;
    } else {
      terminal_writestring("Mount failed.\n");
      is_fat_initialized = false;
    }
    return 0;
  }

  cmd_usage();
  return -1;
}
