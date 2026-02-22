// kernel/cmds/registry.cpp
// Registry of shell commands for Chrysalis OS
// Clean, freestanding-friendly, wrappers based on header prototypes.

#include "registry.h"

#include "../proc/exec.h"
#include "bash_support.h"
#include "beep.h"
#include "buildinfo.h"
#include "cat.h"
#include "cd.h"
#include "chrysver.h"
#include "clear.h"
#include "color.h"
#include "cp.h"
#include "crash.h"
#include "credits.h"
#include "cs.h"
#include "curl.h"
#include "date.h"
#include "disk.h"
#include "echo.h"
#include "elf.h"
#include "elf_crash.h"
#include "elf_debug.h"
#include "fat.h"
#include "fortune.h"
#include "gcc.h"
#include "get.h"
#include "grep.h"
#include "head.h"
#include "help.h"
#include "hexdump.h"
#include "login.h"
#include "ls.h"
#include "mem.h"
#include "mkdir.h"
#include "mv.h"
#include "net.h"
#include "pkg.h"
#include "play.h"
#include "pmm.h"
#include "pwd.h"
#include "reboot.h"
#include "rm.h"
#include "sha256.h"
#include "shutdown.h"
#include "size.h"
#include "sleep.h"
#include "sysfetch.h"
#include "tail.h"
#include "tee.h"
#include "ticks.h"
#include "touch.h"
#include "uptime.h"
#include "vfs.h"
#include "vt_cmd.h"
#include "wc.h"
#include "which.h"
#include "win.h"
#include "write.h"

extern "C" int cmd_bridge(int argc, char **argv);
// Minimal freestanding helpers (no libc)

/*
 * Wrappers:
 *
 * - old-style commands (headers: void cmd_xxx(const char*))
 *   Adapt to shell API int fn(int argc, char** argv)
 *
 * - new-style void commands (headers: void cmd_xxx(int,char**))
 *   Wrap to return int (0)
 *
 * - new-style int commands (headers: int cmd_xxx(int,char**))
 *   Call directly (no wrapper necessary), but we provide thin wrapper to keep
 * table uniform.
 */

/* ----- Generic wrapper implementations ----- */

/* old-style adapter: adapts void func(const char*) to int(int,char**) */
static int wrap_old_style(void (*old_func)(const char *), int argc,
                          char **argv) {
  if (argc <= 1) {
    old_func(0);
    return 0;
  }

  /* build args string in a small stack buffer */
  char args_buffer[256];
  char *p = args_buffer;

  for (int i = 1; i < argc; ++i) {
    if (i > 1) {
      *p++ = ' ';
    }
    const char *src = argv[i];
    while (*src) {
      *p++ = *src++;
      /* avoid overflowing buffer */
      if ((size_t)(p - args_buffer) >= sizeof(args_buffer) - 1)
        break;
    }
    if ((size_t)(p - args_buffer) >= sizeof(args_buffer) - 1)
      break;
  }
  *p = '\0';

  old_func(args_buffer);
  return 0;
}

/* wrapper for new-style void functions */
static int wrap_new_void(void (*fn)(int, char **), int argc, char **argv) {
  fn(argc, argv);
  return 0;
}

/* wrapper for new-style int functions (calls and returns result) */
static int wrap_new_int(int (*fn)(int, char **), int argc, char **argv) {
  return fn(argc, argv);
}

/* ----- Generate wrappers for each command according to header prototypes -----
 */

/* Old-style commands (headers: void cmd_xxx(const char*)) */
static int wrap_cmd_beep(int argc, char **argv) {
  return wrap_old_style(cmd_beep, argc, argv);
}
static int wrap_cmd_buildinfo(int argc, char **argv) {
  return wrap_old_style(cmd_buildinfo, argc, argv);
}
static int wrap_cmd_chrysver(int argc, char **argv) {
  return wrap_old_style(cmd_chrysver, argc, argv);
}
static int wrap_cmd_clear(int argc, char **argv) {
  return wrap_old_style(cmd_clear, argc, argv);
}
static int wrap_cmd_crash(int argc, char **argv) {
  return wrap_old_style(cmd_crash, argc, argv);
}
static int wrap_cmd_bash_support(int argc, char **argv) {
  return wrap_old_style(cmd_bash_support, argc, argv);
}
static int wrap_cmd_credits(int argc, char **argv) {
  return wrap_old_style(cmd_credits, argc, argv);
}
static int wrap_cmd_date(int argc, char **argv) {
  return wrap_old_style(cmd_date, argc, argv);
}
static int wrap_cmd_echo(int argc, char **argv) {
  return wrap_old_style(cmd_echo, argc, argv);
}
static int wrap_cmd_fortune(int argc, char **argv) {
  return wrap_old_style(cmd_fortune, argc, argv);
}
static int wrap_cmd_help(int argc, char **argv) {
  return wrap_old_style(cmd_help, argc, argv);
}
static int wrap_cmd_play(int argc, char **argv) {
  return wrap_old_style(cmd_play, argc, argv);
}
static int wrap_cmd_reboot(int argc, char **argv) {
  return wrap_old_style(cmd_reboot, argc, argv);
}
static int wrap_cmd_shutdown(int argc, char **argv) {
  return wrap_old_style(cmd_shutdown, argc, argv);
}
static int wrap_cmd_sysfetch(int argc, char **argv) {
  return wrap_old_style(cmd_sysfetch, argc, argv);
}
static int wrap_cmd_ticks(int argc, char **argv) {
  return wrap_old_style(cmd_ticks, argc, argv);
}
static int wrap_cmd_touch(int argc, char **argv) {
  return wrap_old_style(cmd_touch, argc, argv);
}
static int wrap_cmd_uptime(int argc, char **argv) {
  return wrap_old_style(cmd_uptime, argc, argv);
}

/* login and mem headers declare old-style; use old wrapper (if implementations
 * are actually new-style you'll need to migrate those implementations) */
static int wrap_cmd_login(int argc, char **argv) {
  return wrap_new_int(cmd_login_main, argc, argv);
}
static int wrap_cmd_add_user(int argc, char **argv) {
  return wrap_new_int(cmd_add_user, argc, argv);
}
static int wrap_cmd_logoff(int argc, char **argv) {
  return wrap_new_int(cmd_logoff, argc, argv);
}
static int wrap_cmd_mem(int argc, char **argv) {
  return wrap_old_style(cmd_mem, argc, argv);
}

/* ----- New-style commands (void) according to headers ----- */
static int wrap_cmd_cat(int argc, char **argv) {
  return wrap_new_void(cmd_cat, argc, argv);
} /* void cmd_cat(int,char**) */
static int wrap_cmd_disk(int argc, char **argv) {
  return wrap_new_void(cmd_disk, argc, argv);
} /* void cmd_disk(int,char**) */
static int wrap_cmd_ls(int argc, char **argv) {
  return wrap_new_void(cmd_ls, argc, argv);
} /* void cmd_ls(int,char**) */
static int wrap_cmd_vfs(int argc, char **argv) {
  return wrap_new_void(cmd_vfs, argc, argv);
} /* void cmd_vfs(int,char**) */

/* ----- New-style commands (int) according to headers ----- */
static int wrap_cmd_fat(int argc, char **argv) {
  return wrap_new_int(cmd_fat, argc, argv);
} /* int cmd_fat(int,char**) */
static int wrap_cmd_elf(int argc, char **argv) {
  return wrap_new_int(cmd_elf, argc, argv);
} /* int cmd_elf(int,char**) */
static int wrap_cmd_elf_debug(int argc, char **argv) {
  return wrap_new_int(cmd_elf_debug, argc, argv);
} /* int cmd_elf_debug(int,char**) */
static int wrap_cmd_elf_crash(int argc, char **argv) {
  return wrap_new_int(cmd_elf_crash, argc, argv);
} /* int cmd_elf_crash(int,char**) */
static int wrap_cmd_pmm(int argc, char **argv) {
  return wrap_new_int(cmd_pmm, argc, argv);
} /* int cmd_pmm(int,char**) */
static int wrap_cmd_write(int argc, char **argv) {
  return wrap_new_int(cmd_write, argc, argv);
} /* int cmd_write(int,char**) */
static int wrap_cmd_cs(int argc, char **argv) {
  return wrap_new_int(cmd_cs_main, argc, argv);
} /* int cmd_cs(int,char**) */
static int wrap_cmd_vt(int argc, char **argv) {
  return wrap_new_int(cmd_vt, argc, argv);
} /* int cmd_vt(int,char**) */
static int wrap_cmd_color(int argc, char **argv) {
  return wrap_new_int(cmd_color, argc, argv);
} /* int cmd_color(int,char**) */
static int wrap_cmd_rm(int argc, char **argv) {
  return wrap_new_int(cmd_rm, argc, argv);
} /* int cmd_rm(int,char**) */
static int wrap_cmd_mkdir(int argc, char **argv) {
  return wrap_new_int(cmd_mkdir, argc, argv);
} /* int cmd_mkdir(int,char**) */
static int wrap_cmd_cd(int argc, char **argv) {
  return wrap_new_int(cmd_cd, argc, argv);
} /* int cmd_cd(int,char**) */
static int wrap_cmd_launch(int argc, char **argv) {
  return wrap_new_int(cmd_launch, argc, argv);
} /* int cmd_launch(int,char**) */
static int wrap_cmd_launch_exit(int argc, char **argv) {
  return wrap_new_int(cmd_launch_exit, argc, argv);
} /* int cmd_launch_exit(int,char**) */
static int wrap_cmd_net(int argc, char **argv) {
  return wrap_new_int(cmd_net, argc, argv);
} /* int cmd_net(int,char**) */
static int wrap_cmd_get(int argc, char **argv) {
  return wrap_new_int(cmd_get, argc, argv);
} /* int cmd_get(int,char**) */
static int wrap_cmd_curl(int argc, char **argv) {
  return wrap_new_int(cmd_curl, argc, argv);
} /* int cmd_curl(int,char**) */
static int wrap_cmd_pkg(int argc, char **argv) {
  return wrap_new_int(cmd_pkg, argc, argv);
} /* int cmd_pkg(int,char**) */
static int wrap_cmd_pwd(int argc, char **argv) {
  return wrap_new_int(cmd_pwd, argc, argv);
} /* int cmd_pwd(int,char**) */
static int wrap_cmd_cp(int argc, char **argv) {
  return wrap_new_int(cmd_cp, argc, argv);
} /* int cmd_cp(int,char**) */
static int wrap_cmd_mv(int argc, char **argv) {
  return wrap_new_int(cmd_mv, argc, argv);
} /* int cmd_mv(int,char**) */
static int wrap_cmd_head(int argc, char **argv) {
  return wrap_new_int(cmd_head, argc, argv);
} /* int cmd_head(int,char**) */
static int wrap_cmd_tail(int argc, char **argv) {
  return wrap_new_int(cmd_tail, argc, argv);
} /* int cmd_tail(int,char**) */
static int wrap_cmd_wc(int argc, char **argv) {
  return wrap_new_int(cmd_wc, argc, argv);
} /* int cmd_wc(int,char**) */
static int wrap_cmd_hexdump(int argc, char **argv) {
  return wrap_new_int(cmd_hexdump, argc, argv);
} /* int cmd_hexdump(int,char**) */
static int wrap_cmd_grep(int argc, char **argv) {
  return wrap_new_int(cmd_grep, argc, argv);
} /* int cmd_grep(int,char**) */
static int wrap_cmd_tee(int argc, char **argv) {
  return wrap_new_int(cmd_tee, argc, argv);
} /* int cmd_tee(int,char**) */
static int wrap_cmd_sha256(int argc, char **argv) {
  return wrap_new_int(cmd_sha256, argc, argv);
} /* int cmd_sha256(int,char**) */
static int wrap_cmd_sleep(int argc, char **argv) {
  return wrap_new_int(cmd_sleep, argc, argv);
} /* int cmd_sleep(int,char**) */
static int wrap_cmd_which(int argc, char **argv) {
  return wrap_new_int(cmd_which, argc, argv);
} /* int cmd_which(int,char**) */
static int wrap_cmd_size(int argc, char **argv) {
  return wrap_new_int(cmd_size, argc, argv);
} /* int cmd_size(int,char**) */
static int wrap_cmd_gcc(int argc, char **argv) {
  return wrap_new_int(cmd_gcc, argc, argv);
}
/* Wrapper for execve */
static int wrap_cmd_exec(int argc, char **argv) {
  if (argc < 2)
    return -1;
  return execve(argv[1], argv + 1, nullptr);
}
static int wrap_cmd_bridge(int argc, char **argv) {
  return wrap_new_int(cmd_bridge, argc, argv);
}

/* ----- Final command table ----- */
/* Command typedef (from registry.h) assumed: typedef struct { const char* name;
 * command_fn fn; } Command; */
Command command_table[] = {
    {"buildinfo", wrap_cmd_buildinfo},
    {"beep", wrap_cmd_beep},
    {"cs", wrap_cmd_cs},
    {"chrysver", wrap_cmd_chrysver},
    {"cd", wrap_cmd_cd},
    {"crash", wrap_cmd_crash},
    {"cat", wrap_cmd_cat},
    {"color", wrap_cmd_color},
    {"clear", wrap_cmd_clear},
    {"curl", wrap_cmd_curl},
    {"credits", wrap_cmd_credits},
    {"date", wrap_cmd_date},
    {"disk", wrap_cmd_disk},
    {"echo", wrap_cmd_echo},
    {"elf", wrap_cmd_elf},
    {"elf-debug", wrap_cmd_elf_debug},
    {"elf-crash", wrap_cmd_elf_crash},
    {"exec", wrap_cmd_exec},
    {"exit", wrap_cmd_shutdown},
    {"fat", wrap_cmd_fat},
    {"fortune", wrap_cmd_fortune},
    {"help", wrap_cmd_help},
    {"get", wrap_cmd_get},
    {"ls", wrap_cmd_ls},
    {"launch", wrap_cmd_launch},
    {"launch-exit", wrap_cmd_launch_exit},
    {"mkdir", wrap_cmd_mkdir},
    {"login", wrap_cmd_login},
    {"mem", wrap_cmd_mem},
    {"net", wrap_cmd_net},
    {"pmm", wrap_cmd_pmm},
    {"pkg", wrap_cmd_pkg},
    {"play", wrap_cmd_play},
    {"pwd", wrap_cmd_pwd},
    {"reboot", wrap_cmd_reboot},
    {"rm", wrap_cmd_rm},
    {"size", wrap_cmd_size},
    {"sleep", wrap_cmd_sleep},
    {"sha256", wrap_cmd_sha256},
    {"shutdown", wrap_cmd_shutdown},
    {"sysfetch", wrap_cmd_sysfetch},
    {"neofetch", wrap_cmd_sysfetch},
    {"bash-support", wrap_cmd_bash_support},
    {"ticks", wrap_cmd_ticks},
    {"tee", wrap_cmd_tee},
    {"tail", wrap_cmd_tail},
    {"touch", wrap_cmd_touch},
    {"uptime", wrap_cmd_uptime},
    {"vfs", wrap_cmd_vfs},
    {"wc", wrap_cmd_wc},
    {"which", wrap_cmd_which},
    {"write", wrap_cmd_write},
    {"win", wrap_cmd_launch},
    {"logoff", wrap_cmd_logoff},
    {"add-user", wrap_cmd_add_user},
    {"vt", wrap_cmd_vt},
    {"cp", wrap_cmd_cp},
    {"mv", wrap_cmd_mv},
    {"head", wrap_cmd_head},
    {"hexdump", wrap_cmd_hexdump},
    {"grep", wrap_cmd_grep},
    {"gcc", wrap_cmd_gcc},
    {"bridge", wrap_cmd_bridge},
};

int command_count = sizeof(command_table) / sizeof(Command);
