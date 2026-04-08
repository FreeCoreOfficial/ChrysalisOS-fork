#include "win.h"
#include "../terminal.h"

extern "C" void wm_cleanup_task_windows(void *task_ptr) { (void)task_ptr; }

extern "C" int cmd_launch_exit(int argc, char **argv) {
  (void)argc;
  (void)argv;
  terminal_writestring("GUI shell removed. Nothing to stop.\n");
  return 0;
}

extern "C" int cmd_logoff(int argc, char **argv) {
  (void)argc;
  (void)argv;
  terminal_writestring("GUI shell removed. Logoff is unavailable.\n");
  return 1;
}

extern "C" int cmd_launch(int argc, char **argv) {
  (void)argc;
  (void)argv;
  terminal_writestring("Old GUI removed.\n");
  return 1;
}

bool win_is_gui_running(void) { return false; }
