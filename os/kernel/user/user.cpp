#include "user.h"
#include "../cmds/fat.h"
#include "../crypto/sha256.h"
#include "../mem/kmalloc.h"
#include "../terminal.h"
#include <stddef.h> /* pentru size_t */
#include <stdint.h>

/* Helper: hex string to byte array */
static void hex2bin(const char *hex, uint8_t *bin, int len) {
  for (int i = 0; i < len; i++) {
    char c = hex[i * 2];
    char c2 = hex[i * 2 + 1];
    uint8_t v = 0;
    if (c >= '0' && c <= '9')
      v = (c - '0') << 4;
    else if (c >= 'a' && c <= 'f')
      v = (c - 'a' + 10) << 4;
    else if (c >= 'A' && c <= 'F')
      v = (c - 'A' + 10) << 4;

    if (c2 >= '0' && c2 <= '9')
      v |= (c2 - '0');
    else if (c2 >= 'a' && c2 <= 'f')
      v |= (c2 - 'a' + 10);
    else if (c2 >= 'A' && c2 <= 'F')
      v |= (c2 - 'A' + 10);

    bin[i] = v;
  }
}

/* mici utilitare string/memcpy pentru freestanding (kernel) */
extern "C" void serial(const char *fmt, ...);

static void *k_memset(void *s, int c, size_t n) {
  unsigned char *p = (unsigned char *)s;
  for (size_t i = 0; i < n; ++i)
    p[i] = (unsigned char)c;
  return s;
}

/* copie sigură: termină cu '\0' (trunchiază dacă e necesar) */
static char *k_strncpy(char *dst, const char *src, size_t maxlen) {
  size_t i = 0;
  if (maxlen == 0)
    return dst;
  while (i < maxlen - 1 && src[i]) {
    dst[i] = src[i];
    i++;
  }
  dst[i] = '\0';
  return dst;
}

/* comparare similară cu strncmp (return 0 dacă egale) */
static int k_strncmp(const char *a, const char *b, size_t n) {
  size_t i = 0;
  if (n == 0)
    return 0;
  while (i < n && a[i] && b[i]) {
    if (a[i] != b[i])
      return (int)((unsigned char)a[i]) - (int)((unsigned char)b[i]);
    i++;
  }
  if (i == n)
    return 0;
  return (int)((unsigned char)a[i]) - (int)((unsigned char)b[i]);
}

/* k_strlen */
static size_t k_strlen(const char *s) {
  size_t len = 0;
  while (s[len])
    len++;
  return len;
}

/* Wrapper-uri pentru confort */
#define memset_k(dest, val, n) k_memset((dest), (val), (n))
#define strncpy_k(dest, src, n) k_strncpy((dest), (src), (n))
#define strncmp_k(a, b, n) k_strncmp((a), (b), (n))

static void user_debug_dump(void);

#define MAX_USERS 8

static user_t users[MAX_USERS];
static int users_count = 0;
static user_t *current_user = 0;

static int find_user_index(const char *name) {
  for (int i = 0; i < users_count; ++i) {
    if (strncmp_k(users[i].name, name, sizeof(users[i].name)) == 0)
      return i;
  }
  return -1;
}

int user_create(const char *name, uint32_t uid, const char *password,
                const char *home, int is_system) {
  if (users_count >= MAX_USERS)
    return -1;
  user_t *u = &users[users_count++];
  u->uid = uid;
  u->gid = (is_system ? 0 : uid);
  u->is_system = is_system ? 1 : 0;
  /* safe copies */
  memset_k(u->name, 0, sizeof(u->name));
  strncpy_k(u->name, name, sizeof(u->name));
  memset_k(u->password, 0, sizeof(u->password));
  if (password)
    strncpy_k(u->password, password, sizeof(u->password));
  memset_k(u->hostname, 0, sizeof(u->hostname));
  strncpy_k(u->hostname, "chrysalis", sizeof(u->hostname)); /* default */
  memset_k(u->home, 0, sizeof(u->home));
  if (home)
    strncpy_k(u->home, home, sizeof(u->home));
  return 0;
}

/* Custom JSON parser helper (minimal) */
/* Custom JSON parser helper (minimal) */
static int json_get_string(const char *json, const char *key, char *out,
                           size_t out_sz) {
  // Simple parser: key" : "value"
  // Does not handle escapes well, but sufficient for our installer output
  if (!json || !key || !out)
    return -1;

  // Construct search key: "key"
  // We don't have strcat easily, so manual scan
  const char *p = json;
  char search_key[64];
  int idx = 0;
  search_key[idx++] = '"';
  const char *k = key;
  while (*k && idx < 60)
    search_key[idx++] = *k++;
  search_key[idx++] = '"';
  search_key[idx] = 0;

  // Find key
  // Manual strstr
  const char *found = 0;
  const char *h = json;
  while (*h) {
    const char *a = h;
    const char *b = search_key;
    while (*a && *b && *a == *b) {
      a++;
      b++;
    }
    if (!*b) {
      found = h;
      break;
    }
    h++;
  }

  if (!found)
    return -1;

  p = found + idx;
  // Skip until :
  while (*p && *p != ':')
    p++;
  if (!*p)
    return -1;
  p++;
  // Skip until "
  while (*p && *p != '"')
    p++;
  if (!*p)
    return -1;
  p++;

  // Copy value
  size_t out_len = 0;
  while (*p && *p != '"' && out_len < out_sz - 1) {
    out[out_len++] = *p++;
  }
  out[out_len] = 0;
  return 0;
}

static void user_load_db(void) {
  // Scan /system/users
  fat_file_info_t entries[16]; // limit to 16 users for now
  int count = fat32_read_directory("/system/users", entries, 16);

  if (count <= 0)
    return;

  for (int i = 0; i < count; i++) {
    if (entries[i].is_dir && entries[i].name[0] != '.') {
      // Found a user directory: /system/users/<name>
      // Check for data.json
      char json_path[128];
      // strcpy / strcat helpers or manual
      // We'll proceed if we have a valid name

      // Build path: /system/users/NAME/data.json
      // Quick robust construction
      char *d = json_path;
      const char *s = "/system/users/";
      while (*s)
        *d++ = *s++;
      s = entries[i].name;
      while (*s)
        *d++ = *s++;
      s = "/data.json";
      while (*s)
        *d++ = *s++;
      *d = 0;

      // Read file
      int fsize = fat32_get_file_size(json_path);
      if (fsize > 0 && fsize < 1024) {
        char *buf = (char *)kmalloc(fsize + 1);
        if (buf) {
          if (fat32_read_file(json_path, buf, fsize) == fsize) {
            buf[fsize] = 0;

            char username[32];
            char pass_hash[128];
            char hostname[32];

            if (json_get_string(buf, "username", username, 32) == 0 &&
                json_get_string(buf, "password", pass_hash, 128) == 0 &&
                json_get_string(buf, "device-name", hostname, 32) == 0) {

              // Create user
              // Use uid = 1000 + i
              user_create(username, 1000 + users_count, pass_hash, "/", 0);

              // Update hostname in the newly created user
              // user_create sets it to default "chrysalis", overwrite it
              int u_idx = find_user_index(username);
              if (u_idx >= 0) {
                strncpy_k(users[u_idx].hostname, hostname, 32);
              }
            } else {
              serial("[USER] Failed to parse JSON in %s\n", json_path);
            }
          }
          kfree(buf);
        }
      }
    }
  }
  if (users_count > 0) {
    terminal_printf("[user] Loaded %d user(s) from disk.\n", users_count);
  }
  // Call at end of load
  user_debug_dump();
}

static void user_debug_dump(void) {
  serial("--- [USER DEBUG] Total users loaded: %d ---\n", users_count);
}

int user_switch(const char *name, const char *password) {
  serial("[LOGIN] Switch request for user '%s'\n", name);
  // user_debug_dump(); // Optional: dump every time

  int idx = find_user_index(name);
  if (idx < 0) {
    serial("[LOGIN] ERROR: User '%s' NOT FOUND.\n", name);
    return -1;
  }
  user_t *u = &users[idx];

  serial("[LOGIN] Found user '%s' (uid=%d). Checking password...\n", u->name,
         u->uid);

  /* If stored password is empty, allow switch without password */
  if (u->password[0] == '\0') {
    current_user = u;
    terminal_printf("User switched to '%s' (no password)\n", u->name);
    serial("[LOGIN] Success (empty password).\n");
    return 0;
  }

  if (!password) {
    serial("[LOGIN] No password provided!\n");
    return -1;
  }

  // Calculate input hash
  uint8_t hash[32];
  sha256_ctx_t sha;
  sha256_init(&sha);

  size_t pass_len = k_strlen(password);
  serial("[LOGIN] Input password len: %d\n", (int)pass_len);

  sha256_update(&sha, (const uint8_t *)password, pass_len);
  sha256_final(&sha, hash);

  char hash_hex[65];
  const char *hex = "0123456789abcdef";
  for (int i = 0; i < 32; i++) {
    hash_hex[i * 2] = hex[(hash[i] >> 4) & 0xF];
    hash_hex[i * 2 + 1] = hex[hash[i] & 0xF];
  }
  hash_hex[64] = 0; // Null terminate

  serial("[LOGIN] Computed Hash: %s\n", hash_hex);
  serial("[LOGIN] Stored Hash:   %s\n", u->password);

  // Try hash comparison first
  if (k_strncmp(u->password, hash_hex, 64) == 0) {
    serial("[LOGIN] Hash Match! Success.\n");
    current_user = u;
    terminal_printf("User switched to '%s'\n", u->name);
    return 0;
  }

  // Fallback: Plain text check (for legacy/system user)
  if (k_strncmp(u->password, password, 128) == 0) {
    serial("[LOGIN] Plaintext Match! Success.\n");
    current_user = u;
    terminal_printf("User switched to '%s'\n", u->name);
    return 0;
  }

  serial("[LOGIN] Password Mismatch.\n");
  // Dump diff
  for (int i = 0; i < 64; i++) {
    if (u->password[i] != hash_hex[i]) {
      serial("   Diff at char %d: stored '%c'(%x) vs computed '%c'(%x)\n", i,
             u->password[i], u->password[i], hash_hex[i], hash_hex[i]);
      break;
    }
  }

  return -1;
}

user_t *user_get_current(void) { return current_user; }

void user_init(void) {
  /* clear */
  users_count = 0;
  current_user = 0;

  /* Load users from disk */
  user_load_db();

  /* default system user fallback if no users found */
  if (users_count == 0) {
    user_create("system", 0, "123", "/", 1);
  }

  /* Don't auto-login to system user if we have real users */
  /* If users loaded, do not set current_user to allow login prompt */
  if (users_count > 0) {
    // current_user = &users[0]; // Disable auto-login
    current_user = 0;
  }
}
