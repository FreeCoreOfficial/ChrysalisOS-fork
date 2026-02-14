#include "command_exec.h"
#include "registry.h"
#include "../string.h"
#include "../terminal.h"
#include <stddef.h>
#include <stdint.h>

#define CMD_EXEC_MAX_LINE 512
#define CMD_EXEC_MAX_ARGS 32

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

extern "C" int cmd_exec_capture(const char *line, char *out, uint32_t out_cap) {
  if (!line || !out || out_cap == 0)
    return -1;

  out[0] = 0;

  char line_buf[CMD_EXEC_MAX_LINE];
  strncpy(line_buf, line, sizeof(line_buf) - 1);
  line_buf[sizeof(line_buf) - 1] = 0;
  trim_inplace(line_buf);

  if (line_buf[0] == 0)
    return 0;

  char *argv[CMD_EXEC_MAX_ARGS];
  int argc = tokenize_simple(line_buf, argv, CMD_EXEC_MAX_ARGS);
  if (argc <= 0)
    return 0;

  size_t written = 0;
  uint32_t cap_for_capture = out_cap - 1; // reserve NUL terminator
  terminal_start_capture(out, cap_for_capture, &written);

  int found = 0;
  for (int i = 0; i < command_count; i++) {
    if (strcmp(command_table[i].name, argv[0]) == 0) {
      command_table[i].func(argc, argv);
      found = 1;
      break;
    }
  }

  if (!found) {
    terminal_writestring("Unknown command: ");
    terminal_writestring(argv[0]);
    terminal_writestring("\n");
  }

  terminal_end_capture();

  if (written > cap_for_capture)
    written = cap_for_capture;
  out[written] = 0;

  return (int)written;
}
