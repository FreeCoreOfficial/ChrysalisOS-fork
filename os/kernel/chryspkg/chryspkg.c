/* kernel/chryspkg/chryspkg.c */
#include "chryspkg.h"
#include "../cmds/fat.h"
#include "../drivers/serial.h"
#include "../include/stdio.h"
#include "../include/string.h"
#include "../mem/kmalloc.h"

#define CATALOG_PATH "/system/pkg/catalog.json"
#define INSTALLED_DIR "/system/pkg/installed"
#define MAX_JSON_SIZE (16 * 1024)

static char *catalog_json = NULL;
static char repo_url[128] = {0};

extern void serial(const char *fmt, ...);

static char *json_find_value(const char *json, const char *key, char *out_buf,
                             int max_len) {
  if (!json || !key || !out_buf || max_len <= 1)
    return NULL;

  char search[64];
  search[0] = '"';
  int i = 0;
  while (key[i] && i < 60) {
    search[i + 1] = key[i];
    i++;
  }
  search[i + 1] = '"';
  search[i + 2] = ':';
  search[i + 3] = 0;

  char *pos = strstr((char *)json, search);
  if (!pos)
    return NULL;

  pos += strlen(search);
  while (*pos == ' ' || *pos == '\t' || *pos == '\n')
    pos++;

  if (*pos != '"')
    return NULL;

  pos++;
  int j = 0;
  while (*pos && *pos != '"' && j < max_len - 1)
    out_buf[j++] = *pos++;
  out_buf[j] = 0;
  return out_buf;
}

void chryspkg_init(void) {
  serial("[PKG] init\n");
  fat_automount();

  if (!fat32_directory_exists("/system"))
    fat32_create_directory("/system");
  if (!fat32_directory_exists("/system/pkg"))
    fat32_create_directory("/system/pkg");
  if (!fat32_directory_exists(INSTALLED_DIR))
    fat32_create_directory(INSTALLED_DIR);

  if (fat32_get_file_size(CATALOG_PATH) < 0) {
    const char *def_cat = "{\n"
                          "  \"catalog_version\": 2,\n"
                          "  \"repository\": \"native://chrysalis\",\n"
                          "  \"apps\": []\n"
                          "}\n";
    fat32_create_file(CATALOG_PATH, def_cat, strlen(def_cat));
  }

  FILE *f = fopen(CATALOG_PATH, "r");
  if (!f) {
    serial("[PKG] Error: Cannot open catalog at %s\n", CATALOG_PATH);
    return;
  }

  fseek(f, 0, SEEK_END);
  long size = ftell(f);
  fseek(f, 0, SEEK_SET);
  if (size <= 0 || size > MAX_JSON_SIZE) {
    serial("[PKG] Error: Catalog size invalid (%ld)\n", size);
    fclose(f);
    return;
  }

  catalog_json = (char *)kmalloc((size_t)size + 1);
  if (!catalog_json) {
    fclose(f);
    serial("[PKG] Error: OOM loading catalog\n");
    return;
  }

  fread(catalog_json, 1, (size_t)size, f);
  catalog_json[size] = 0;
  fclose(f);

  if (json_find_value(catalog_json, "repository", repo_url, sizeof(repo_url)))
    serial("[PKG] repo: %s\n", repo_url);
}

void chryspkg_list(void) {
  if (!catalog_json) {
    serial("[PKG] Error: Catalog not loaded\n");
    return;
  }

  char *apps = strstr(catalog_json, "\"apps\"");
  if (!apps) {
    serial("[PKG] No catalog entries\n");
    return;
  }

  char *list_start = strchr(apps, '[');
  char *list_end = strchr(apps, ']');
  if (!list_start || !list_end || list_end <= list_start + 1) {
    serial("[PKG] No installable packages. Native apps are built into the system.\n");
    return;
  }

  serial("[PKG] Listing native catalog entries:\n");
}
