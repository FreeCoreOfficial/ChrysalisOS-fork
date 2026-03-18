#include "../terminal.h"
#include "../string.h"

#ifndef CHRYVER
#define CHRYVER "chrysver-unknown"
#endif

struct linux_utsname {
  char sysname[65];
  char nodename[65];
  char release[65];
  char version[65];
  char machine[65];
  char domainname[65];
};

static void uname_fill(linux_utsname *u, bool linux_style) {
  if (!u)
    return;
  if (linux_style) {
    strncpy(u->sysname, "Linux", sizeof(u->sysname) - 1);
    strncpy(u->release, "6.1.0-chrysalis", sizeof(u->release) - 1);
    strncpy(u->version, "#1 PREEMPT " __DATE__ " " __TIME__,
            sizeof(u->version) - 1);
    strncpy(u->machine, "i386", sizeof(u->machine) - 1);
  } else {
    strncpy(u->sysname, "ChrysalisOS", sizeof(u->sysname) - 1);
    strncpy(u->release, CHRYVER, sizeof(u->release) - 1);
    strncpy(u->version, __DATE__ " " __TIME__, sizeof(u->version) - 1);
    strncpy(u->machine, "i386", sizeof(u->machine) - 1);
  }
  strncpy(u->nodename, "chrysalis", sizeof(u->nodename) - 1);
  strncpy(u->domainname, "(none)", sizeof(u->domainname) - 1);
  u->sysname[sizeof(u->sysname) - 1] = 0;
  u->nodename[sizeof(u->nodename) - 1] = 0;
  u->release[sizeof(u->release) - 1] = 0;
  u->version[sizeof(u->version) - 1] = 0;
  u->machine[sizeof(u->machine) - 1] = 0;
  u->domainname[sizeof(u->domainname) - 1] = 0;
}

static void skip_spaces(const char **p) {
  while (**p == ' ' || **p == '\t')
    (*p)++;
}

static int next_token(const char **p, char *out, int out_sz) {
  if (!p || !*p || !out || out_sz <= 0)
    return 0;
  skip_spaces(p);
  if (**p == 0)
    return 0;
  int i = 0;
  while (**p && **p != ' ' && **p != '\t') {
    if (i < out_sz - 1)
      out[i++] = **p;
    (*p)++;
  }
  out[i] = 0;
  return 1;
}

extern "C" void cmd_uname(const char *args) {
  bool opt_s = false;
  bool opt_n = false;
  bool opt_r = false;
  bool opt_v = false;
  bool opt_m = false;
  bool opt_a = false;
  bool linux_style = false;

  const char *p = args ? args : "";
  char tok[64];
  while (next_token(&p, tok, sizeof(tok))) {
    if (tok[0] == '-' && tok[1] == '-') {
      if (strcmp(tok, "--chrysalis") == 0 || strcmp(tok, "--real") == 0)
        linux_style = false;
      if (strcmp(tok, "--all") == 0)
        opt_a = true;
    } else if (tok[0] == '-') {
      for (int i = 1; tok[i]; i++) {
        switch (tok[i]) {
        case 'a':
          opt_a = true;
          break;
        case 's':
          opt_s = true;
          break;
        case 'n':
          opt_n = true;
          break;
        case 'r':
          opt_r = true;
          break;
        case 'v':
          opt_v = true;
          break;
        case 'm':
          opt_m = true;
          break;
        default:
          break;
        }
      }
    }
  }

  if (opt_a) {
    opt_s = opt_n = opt_r = opt_v = opt_m = true;
  }
  if (!opt_s && !opt_n && !opt_r && !opt_v && !opt_m) {
    opt_s = true;
  }

  linux_utsname u;
  uname_fill(&u, linux_style);

  bool first = true;
  auto emit = [&](const char *s) {
    if (!first)
      terminal_writestring(" ");
    terminal_writestring(s);
    first = false;
  };

  if (opt_s)
    emit(u.sysname);
  if (opt_n)
    emit(u.nodename);
  if (opt_r)
    emit(u.release);
  if (opt_v)
    emit(u.version);
  if (opt_m)
    emit(u.machine);

  terminal_writestring("\n");
}
