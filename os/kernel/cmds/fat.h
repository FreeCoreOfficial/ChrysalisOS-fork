#pragma once
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

int cmd_fat(int argc, char **argv);

/* Încearcă să monteze automat prima partiție FAT32 găsită */
void fat_automount(void);

/* Force-set current FAT mount (used by installer) */
void fat32_set_mounted(uint32_t lba, char letter);

/* Listează conținutul unui director (path="/" pentru root) */
void fat32_list_directory(const char* path);

/* Structure for directory listing API */
typedef struct {
    char name[256];
    uint32_t size;
    uint8_t is_dir;
} fat_file_info_t;

/* Read directory entries into a buffer. Returns number of entries read. */
int fat32_read_directory(const char* path, fat_file_info_t* out, int max_entries);

/* Citește un fișier complet (limitat la max_size) */
int fat32_read_file(const char* path, void* buf, uint32_t max_size);

/* Obține dimensiunea unui fișier (sau -1 dacă nu există) */
int32_t fat32_get_file_size(const char* path);

/* Citește dintr-un fișier de la un offset specificat (pentru fișiere mari) */
int fat32_read_file_offset(const char* path, void* buf, uint32_t size, uint32_t offset);

/* Creează un fișier (sau suprascrie) */
int fat32_create_file(const char* path, const void* data, uint32_t size);

/* Creează un fișier cu verificare readback */
int fat32_create_file_verified(const char* path, const void* data, uint32_t size, int verify);

/* Create an empty file with allocated clusters (no data written) */
int fat32_create_file_alloc(const char* path, uint32_t size);

/* Write data into an existing file at an offset */
int fat32_write_file_offset(const char* path, const void* data, uint32_t size, uint32_t offset, int verify);

/* Delete a file from the root directory */
int fat32_delete_file(const char* path);

/* Create a directory in the root directory */
int fat32_create_directory(const char* path);

/* Create a directory with optional readback verification */
int fat32_create_directory_verified(const char* path, int verify);
/* fat32_create_directory* error codes:
 * -101 invalid path
 * -102 OOM sector buffer
 * -103 BPB read failed
 * -104 invalid BPB
 * -105 resolve_parent failed
 * -106 invalid name length
 * -107 alloc cluster failed
 * -108 set EOC failed
 * -12  dot sector write failed
 * -13  clear sector write failed
 * -21  LFN write failed
 * -22  short entry write failed
 * -23  no free entry run
 * -24  extend directory failed
 * -25  short alias failed
 * -26  LFN sector read failed
 * -27  short entry sector read failed
 */

/* Check if a directory exists */
int fat32_directory_exists(const char* path);

/* Rename/move within same directory */
int fat32_rename(const char* src, const char* dst);

/* Format a partition with FAT32 */
int fat32_format(uint32_t lba, uint32_t sector_count, const char* label);

/* Get file size (returns -1 if not found) */
int32_t fat32_get_file_size(const char* path);

#ifdef __cplusplus
}
#endif
