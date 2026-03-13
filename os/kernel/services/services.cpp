#include "services.h"
#include "../cmds/fat.h"
#include "../proc/exec.h"
#include "../string.h"
#include "../terminal.h"

#define SERVICES_DIR "/system/services"
#define SERVICE_MAX_ARGS 16

typedef struct {
  char exec[256];
  char args[256];
  int enabled;
  int requires_gui;
} service_cfg_t;

static int g_services_gui_ready = 0;

void services_set_gui_ready(int enabled) { g_services_gui_ready = enabled; }

static int is_space_char(char c) {
  return c == ' ' || c == '\t' || c == '\n' || c == '\r';
}

static void trim_inplace(char *s) {
  if (!s)
    return;
  char *start = s;
  while (*start && is_space_char(*start))
    start++;
  if (start != s) {
    size_t i = 0;
    while (start[i]) {
      s[i] = start[i];
      i++;
    }
    s[i] = 0;
  }
  size_t len = strlen(s);
  while (len > 0 && is_space_char(s[len - 1])) {
    s[len - 1] = 0;
    len--;
  }
}

static char lower_ascii(char c) {
  if (c >= 'A' && c <= 'Z')
    return (char)(c + 32);
  return c;
}

static bool has_ext_ci(const char *name, const char *ext) {
  if (!name || !ext)
    return false;
  size_t nlen = strlen(name);
  size_t elen = strlen(ext);
  if (nlen < elen)
    return false;
  const char *p = name + (nlen - elen);
  for (size_t i = 0; i < elen; i++) {
    if (lower_ascii(p[i]) != lower_ascii(ext[i]))
      return false;
  }
  return true;
}

static int parse_bool(const char *v) {
  if (!v || !*v)
    return 0;
  char c = lower_ascii(*v);
  if (c == '0' || c == 'n' || c == 'f')
    return 0;
  return 1;
}

static int tokenize_simple(char *line, char **argv, int max_args) {
  if (!line || !argv || max_args <= 0)
    return 0;

  int argc = 0;
  char *p = line;
  while (*p && argc < max_args) {
    while (*p && is_space_char(*p))
      p++;
    if (!*p)
      break;
    if (*p == '"') {
      p++;
      argv[argc++] = p;
      while (*p && *p != '"')
        p++;
      if (*p == '"') {
        *p = 0;
        p++;
      }
    } else {
      argv[argc++] = p;
      while (*p && !is_space_char(*p))
        p++;
      if (*p) {
        *p = 0;
        p++;
      }
    }
  }
  return argc;
}

static void append_str(char *dst, size_t cap, const char *src) {
  if (!dst || !src || cap == 0)
    return;
  size_t len = strlen(dst);
  size_t i = 0;
  while (src[i] && (len + 1) < cap) {
    dst[len++] = src[i++];
  }
  dst[len] = 0;
}

static void service_start_exec(const service_cfg_t *svc) {
  if (!svc || !svc->exec[0])
    return;

  char exec_buf[256];
  strncpy(exec_buf, svc->exec, sizeof(exec_buf) - 1);
  exec_buf[sizeof(exec_buf) - 1] = 0;

  char args_buf[256];
  args_buf[0] = 0;
  if (svc->args[0]) {
    strncpy(args_buf, svc->args, sizeof(args_buf) - 1);
    args_buf[sizeof(args_buf) - 1] = 0;
  }

  char *argv[SERVICE_MAX_ARGS];
  int argc = 0;
  argv[argc++] = exec_buf;
  if (args_buf[0]) {
    argc += tokenize_simple(args_buf, &argv[argc],
                            SERVICE_MAX_ARGS - argc - 1);
  }
  argv[argc] = nullptr;

  terminal_printf("[services] starting: %s\n", exec_buf);
  execve(exec_buf, argv, nullptr);
}

static void parse_service_file(const char *path, service_cfg_t *out) {
  if (!path || !out)
    return;

  out->exec[0] = 0;
  out->args[0] = 0;
  out->enabled = 1;
  out->requires_gui = 0;

  char buf[1024];
  int bytes = fat32_read_file(path, buf, sizeof(buf) - 1);
  if (bytes <= 0)
    return;
  buf[bytes] = 0;

  char *p = buf;
  while (*p) {
    char *line = p;
    while (*p && *p != '\n' && *p != '\r')
      p++;
    char saved = *p;
    *p = 0;

    trim_inplace(line);
    if (line[0] && line[0] != '#' && line[0] != ';') {
      char *eq = strchr(line, '=');
      if (eq) {
        *eq = 0;
        char *key = line;
        char *val = eq + 1;
        trim_inplace(key);
        trim_inplace(val);
        if (strcmp(key, "Exec") == 0) {
          strncpy(out->exec, val, sizeof(out->exec) - 1);
          out->exec[sizeof(out->exec) - 1] = 0;
        } else if (strcmp(key, "Args") == 0) {
          strncpy(out->args, val, sizeof(out->args) - 1);
          out->args[sizeof(out->args) - 1] = 0;
        } else if (strcmp(key, "Enabled") == 0 ||
                   strcmp(key, "Autostart") == 0) {
          out->enabled = parse_bool(val);
        } else if (strcmp(key, "RequiresGUI") == 0 ||
                   strcmp(key, "GuiOnly") == 0) {
          out->requires_gui = parse_bool(val);
        }
      }
    }

    *p = saved;
    while (*p == '\n' || *p == '\r')
      p++;
  }
}

void services_start(void) {
  static int started = 0;
  if (started)
    return;
  started = 1;

  fat_automount();
  if (!fat32_directory_exists("/system")) {
    fat32_create_directory("/system");
  }
  if (!fat32_directory_exists(SERVICES_DIR)) {
    fat32_create_directory(SERVICES_DIR);
  }

  fat_file_info_t files[32];
  int count = fat32_read_directory(SERVICES_DIR, files, 32);
  if (count <= 0)
    return;

  for (int i = 0; i < count; i++) {
    if (!files[i].name[0])
      continue;
    if (files[i].is_dir)
      continue;
    if (!has_ext_ci(files[i].name, ".srv"))
      continue;

    char path[256];
    strncpy(path, SERVICES_DIR, sizeof(path) - 1);
    path[sizeof(path) - 1] = 0;
    size_t len = strlen(path);
    if (len + 1 < sizeof(path) && path[len - 1] != '/') {
      path[len++] = '/';
      path[len] = 0;
    }
    append_str(path, sizeof(path), files[i].name);

    service_cfg_t cfg;
    parse_service_file(path, &cfg);
    if (!cfg.enabled || !cfg.exec[0])
      continue;
    if (cfg.requires_gui && !g_services_gui_ready)
      continue;

    service_start_exec(&cfg);
  }
}
