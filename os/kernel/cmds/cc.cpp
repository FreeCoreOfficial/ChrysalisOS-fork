#include "cc.h"
#include "../proc/exec.h"
#include "../terminal.h"

static void cc_usage() {
  terminal_writestring("Usage: cc <file.c> -o <output>\n");
  terminal_writestring("Note: requires TinyCC at /system/bin/tcc\n");
}

int cmd_cc(int argc, char **argv) {
  if (argc < 2) {
    cc_usage();
    return -1;
  }

  const char *tcc_path = "/system/bin/tcc";

  char *exec_argv[32];
  if (argc >= (int)(sizeof(exec_argv) / sizeof(exec_argv[0]) - 1)) {
    terminal_writestring("cc: too many arguments\n");
    return -1;
  }

  exec_argv[0] = (char *)tcc_path;
  for (int i = 1; i < argc; i++) {
    exec_argv[i] = argv[i];
  }
  exec_argv[argc] = 0;

  int pid = execve(tcc_path, exec_argv, 0);
  if (pid < 0) {
    terminal_writestring("cc: TinyCC not found at /system/bin/tcc\n");
    terminal_writestring(
        "cc: build it via os/ported-langs/C/toolchain and rebuild the ISO\n");
    return -1;
  }

  return 0;
}
