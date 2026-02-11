#include "rm.h"
#include "../terminal.h"
#include "fat.h"
#include "pathutil.h"

extern "C" int cmd_rm(int argc, char **argv) {
  if (argc < 2) {
    terminal_writestring("Usage: rm <filename>\n");
    return -1;
  }

  char path[256];
  cmd_resolve_path(argv[1], path, sizeof(path));

  // Security: Prevent deletion of critical system directories
  if (path[0] == '/' && path[1] == 's' && path[2] == 'y' && path[3] == 's' &&
      path[4] == 't' && path[5] == 'e' && path[6] == 'm' &&
      (path[7] == '/' || path[7] == '\0')) {
    terminal_writestring("Error: Cannot delete /system - critical system "
                         "directory protected.\n");
    return -1;
  }

  if (path[0] == '/' && path[1] == 'b' && path[2] == 'o' && path[3] == 'o' &&
      path[4] == 't' && (path[5] == '/' || path[5] == '\0')) {
    terminal_writestring(
        "Error: Cannot delete /boot - critical boot directory protected.\n");
    return -1;
  }

  fat_automount();

  int res = fat32_delete_file(path);
  if (res == 0) {
    terminal_writestring("File deleted.\n");
    return 0;
  } else {
    terminal_writestring(
        "Error: Could not delete file (not found or error).\n");
    return -1;
  }
}
