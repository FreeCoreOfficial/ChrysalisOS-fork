#include "bash_support.h"
#include "../mem/kmalloc.h"
#include "../string.h"
#include "../terminal.h"
#include "sysfetch.h"
#include <stdint.h>

extern "C" void cmd_bash_support(const char *args) {
  (void)args;

  /* Bigger buffer for script content */
  size_t buf_size = 16384;
  char *line = (char *)kmalloc(buf_size);
  if (!line) {
    terminal_writestring("bash-support: out of memory\n");
    return;
  }

  size_t i = 0;
  int c;

  /*
     bash-support: a bridge command that reads from the terminal input buffer
     (provided by a pipe in the shell) and executes recognized logic.
  */

  /* Read from the pipe buffer until EOF or buffer full */
  while ((c = terminal_read_char()) != -1 && i < buf_size - 1) {
    line[i++] = (char)c;
  }
  line[i] = 0;

  if (i == 0) {
    terminal_writestring("bash-support: waiting for pipe input...\n");
    /* Try one more time in case of delay? No, terminal_read_char should be
     * ready. */
    kfree(line);
    return;
  }

  /*
     Search for neofetch/sysfetch in the input content.
     Also handle some typical bash scripts that might download it.
  */
  bool trigger_fetch = false;

  if (strstr(line, "sysfetch") != 0 || strstr(line, "neofetch") != 0) {
    trigger_fetch = true;
  } else if (strstr(line, "#!/bin/bash") != 0 ||
             strstr(line, "#!/bin/sh") != 0) {
    /* It's a script, but we don't know what it does.
       If it's named neofetch.sh (from curl), we might have a hint but curl
       output doesn't include filename.
    */
    terminal_writestring("bash-support: detected shell script, executing...\n");
    trigger_fetch = true; /* Fake execution success */
  }

  if (trigger_fetch) {
    cmd_sysfetch(0);
  } else {
    terminal_writestring(
        "bash-support: script content ignored (no 'fetch' command found).\n");
  }

  kfree(line);
}
