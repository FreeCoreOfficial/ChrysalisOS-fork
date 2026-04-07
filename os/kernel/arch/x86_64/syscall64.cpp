#include "syscall64.h"
#include "../../hardware/msr.h"
#include "../../include/chrysalis/syscall_nums.h"
#include "../../drivers/serial.h"
#include "../../fs/vfs/fs_ops.h"
#include "../../fs/vfs/vfs.h"
#include "../../fs/ramfs/ramfs.h"
#include "../../fs/pipe/pipe.h"
#include "../../linux_compat/linux_syscall_x86_64.h"
#include "../../mem/kmalloc.h"
#include "../../sched/task64.h"
#include "../../string.h"
#include "../../time/clock.h"
#include "../../time/timer.h"
#include "../i386/io.h"
#include "../../fs/vfs/vfs.h"
#include <stddef.h>

#define MSR_EFER 0xC0000080u
#define MSR_STAR 0xC0000081u
#define MSR_LSTAR 0xC0000082u
#define MSR_FMASK 0xC0000084u

extern "C" void syscall64_entry(void);

typedef uint64_t (*syscall64_fn)(uint64_t, uint64_t, uint64_t, uint64_t,
                                 uint64_t, uint64_t);

extern "C" void vga_puts_k64(const char *s);
extern "C" void vga_putc_k64(char c);


static constexpr uint64_t k_sys_enosys = (uint64_t)-38;
static constexpr uint64_t k_linux_enoent = (uint64_t)-2;
static constexpr uint64_t k_linux_enfile = (uint64_t)-23;
static constexpr uint64_t k_linux_ocreat = 0x40;
static constexpr uint64_t k_linux_ononblock = 0x800;
static constexpr uint64_t k_linux_ocloexec = 0x80000;
static constexpr uint8_t k_linux_fd_cloexec = 1;
static constexpr int k_syscall_table_size = 128;
static syscall64_fn g_syscall_table[k_syscall_table_size];
static file_t *g_boot_files[MAX_FILES_PER_PROCESS];
static uint8_t g_boot_fd_flags[MAX_FILES_PER_PROCESS];
static file_t g_file_pool[MAX_FILES_PER_PROCESS];
static uint8_t g_file_used[MAX_FILES_PER_PROCESS];
static int g_linux_abi_mode = 0;
static syscall64_state_t *g_syscall_state = nullptr;

static void append_path_component(char *dst, uint64_t cap, const char *src) {
  uint64_t len = (uint64_t)strlen(dst);
  uint64_t i = 0;
  while (src[i] && (len + 1) < cap) {
    dst[len++] = src[i++];
  }
  dst[len] = 0;
}

static void normalize_abs_path(char *path) {
  char temp[TASK64_CWD_MAX * 2];
  char *parts[64];
  int count = 0;

  strncpy(temp, path, sizeof(temp) - 1);
  temp[sizeof(temp) - 1] = 0;

  char *p = temp;
  if (*p == '/')
    ++p;
  char *token = p;
  while (*p) {
    if (*p == '/') {
      *p = 0;
      if (*token && strcmp(token, ".") != 0) {
        if (strcmp(token, "..") == 0) {
          if (count > 0)
            --count;
        } else if (count < (int)(sizeof(parts) / sizeof(parts[0]))) {
          parts[count++] = token;
        }
      }
      token = p + 1;
    }
    ++p;
  }
  if (*token && strcmp(token, ".") != 0) {
    if (strcmp(token, "..") == 0) {
      if (count > 0)
        --count;
    } else if (count < (int)(sizeof(parts) / sizeof(parts[0]))) {
      parts[count++] = token;
    }
  }

  path[0] = '/';
  path[1] = 0;
  for (int i = 0; i < count; ++i) {
    if (i > 0)
      append_path_component(path, TASK64_CWD_MAX, "/");
    append_path_component(path, TASK64_CWD_MAX, parts[i]);
  }
}

int syscall64_resolve_path(const char *path, char *out, uint64_t out_size) {
  if (!path || !out || out_size == 0)
    return -1;

  if (path[0] == 0)
    return -1;

  out[0] = 0;
  if (path[0] == '/') {
    strncpy(out, path, (size_t)out_size - 1);
    out[out_size - 1] = 0;
    normalize_abs_path(out);
    return 0;
  }

  const char *cwd = "/";
  if (task64_t *t = task64_current()) {
    if (t->cwd[0])
      cwd = t->cwd;
  }

  strncpy(out, cwd, (size_t)out_size - 1);
  out[out_size - 1] = 0;
  uint64_t len = (uint64_t)strlen(out);
  if (len == 0) {
    out[0] = '/';
    out[1] = 0;
  } else if (out[len - 1] != '/') {
    append_path_component(out, out_size, "/");
  }
  append_path_component(out, out_size, path);
  normalize_abs_path(out);
  return 0;
}

void syscall64_prepare_exec_transition(void) {
  g_syscall_state = nullptr;
}

static void register_syscall(uint32_t num, syscall64_fn fn) {
  if (num < (uint32_t)k_syscall_table_size)
    g_syscall_table[num] = fn;
}

static uint64_t sys_write64(uint64_t fd, uint64_t buf, uint64_t size,
                            uint64_t, uint64_t, uint64_t) {
  if (!buf || size == 0)
    return 0;
  const char *s = (const char *)(uintptr_t)buf;
  file_t *f = nullptr;
  if (fd < (uint64_t)MAX_FILES_PER_PROCESS)
    f = syscall64_get_file((int)fd);
  if (!f && (fd == 1 || fd == 2 || fd == 0)) {
    for (uint64_t i = 0; i < size; ++i) {
      serial_write(s[i]);
      vga_putc_k64(s[i]);
    }
    return size;
  }
  if (fd >= (uint64_t)MAX_FILES_PER_PROCESS)
    return k_sys_enosys;
  if (!f || !f->node || !f->node->ops || !f->node->ops->write)
    return k_sys_enosys;
  int bytes;
  if (pipe_is_vnode(f->node)) {
    bytes = pipe_write_file(f->node, (f->flags & (int)k_linux_ononblock) != 0,
                            (const uint8_t *)s, (uint32_t)size);
  } else {
    bytes = f->node->ops->write(f->node, f->offset,
                                (const uint8_t *)s, (uint32_t)size);
  }
  if (bytes > 0)
    f->offset += (uint32_t)bytes;
  return (uint64_t)bytes;
}

static uint64_t sys_read64(uint64_t fd, uint64_t buf, uint64_t size,
                           uint64_t, uint64_t, uint64_t) {
  if (!buf || size == 0)
    return 0;
  serial_write_string("[K64] sys_read64 fd=");
  serial_printf("%u", (unsigned int)fd);
  serial_write_string(" size=");
  serial_printf("%u", (unsigned int)size);
  file_t *f = nullptr;
  if (fd < (uint64_t)MAX_FILES_PER_PROCESS)
    f = syscall64_get_file((int)fd);
  if (!f && fd == 0) {
    serial_write_string(" src=stdin\r\n");
    char *out = (char *)(uintptr_t)buf;
    for (uint64_t i = 0; i < size; ++i) {
      out[i] = serial_read();
    }
    return size;
  }
  if (fd >= (uint64_t)MAX_FILES_PER_PROCESS)
  {
    serial_write_string(" src=badfd\r\n");
    return k_sys_enosys;
  }
  if (!f || !f->node || !f->node->ops || !f->node->ops->read)
  {
    serial_write_string(" src=unreadable\r\n");
    return k_sys_enosys;
  }
  serial_write_string(" src=");
  serial_write_string(f->node->name ? f->node->name : "(noname)");
  serial_write_string("\r\n");
  int bytes;
  if (pipe_is_vnode(f->node)) {
    bytes = pipe_read_file(f->node, (f->flags & (int)k_linux_ononblock) != 0,
                           (uint8_t *)buf, (uint32_t)size);
  } else {
    bytes = f->node->ops->read(f->node, f->offset, (uint8_t *)buf, (uint32_t)size);
  }
  if (bytes > 0)
    f->offset += (uint32_t)bytes;
  return (uint64_t)bytes;
}

static uint64_t sys_open64(uint64_t path_ptr, uint64_t flags, uint64_t,
                           uint64_t, uint64_t, uint64_t) {
  if (!path_ptr)
    return k_sys_enosys;
  const char *path = (const char *)(uintptr_t)path_ptr;
  char resolved[TASK64_CWD_MAX];
  if (syscall64_resolve_path(path, resolved, sizeof(resolved)) < 0)
    return k_linux_enoent;
  vnode_t *node = vfs_resolve(resolved);
  if (!node) {
    if ((flags & k_linux_ocreat) && resolved[0] == '/') {
      ramfs_create_file(resolved, nullptr, 0);
      node = vfs_resolve(resolved);
    }
  }
  if (!node) {
    serial_write_string("[K64] sys_open64: vfs_resolve failed for '");
    serial_write_string(resolved);
    serial_write_string("'\r\n");
    return k_linux_enoent;
  }
  serial_write_string("[K64] sys_open64: vfs_resolve SUCCESS for '");
  serial_write_string(resolved);
  serial_write_string("'\r\n");


  if (node->ops && node->ops->open) {
    if (node->ops->open(node) < 0)
      return (uint64_t)-5; // EIO or similar
  }

  for (int i = 3; i < MAX_FILES_PER_PROCESS; ++i) {
    if (!syscall64_get_file(i) && !g_file_used[i]) {
      file_t *f = &g_file_pool[i];
      memset(f, 0, sizeof(*f));
      f->node = node;
      f->offset = 0;
      f->flags = (int)(flags & ~k_linux_ocloexec);
      f->refcount = 1;
      g_file_used[i] = 1;
      syscall64_set_fd_flags(i, (flags & k_linux_ocloexec) ? k_linux_fd_cloexec : 0);
      syscall64_set_file(i, f);
      return (uint64_t)i;
    }
  }
  return k_linux_enfile;
}

static uint64_t sys_close64(uint64_t fd, uint64_t, uint64_t, uint64_t,
                            uint64_t, uint64_t) {
  if (fd >= (uint64_t)MAX_FILES_PER_PROCESS)
    return k_sys_enosys;
  file_t *f = syscall64_get_file((int)fd);
  if (!f)
    return k_sys_enosys;
  syscall64_set_file((int)fd, nullptr);
  syscall64_set_fd_flags((int)fd, 0);
  if (f->refcount > 0)
    --f->refcount;
  if (f->refcount <= 0) {
    if (f->node && f->node->ops && f->node->ops->close)
      f->node->ops->close(f->node);
    uintptr_t base = (uintptr_t)&g_file_pool[0];
    uintptr_t end = (uintptr_t)&g_file_pool[MAX_FILES_PER_PROCESS];
    uintptr_t p = (uintptr_t)f;
    if (p >= base && p < end) {
      size_t idx = (size_t)((p - base) / sizeof(file_t));
      if (idx < MAX_FILES_PER_PROCESS)
        g_file_used[idx] = 0;
    } else {
      kfree(f);
    }
  }
  return 0;
}

static uint64_t sys_ioctl64(uint64_t fd, uint64_t cmd, uint64_t arg,
                            uint64_t, uint64_t, uint64_t) {
  if (fd >= (uint64_t)MAX_FILES_PER_PROCESS)
    return k_sys_enosys;
  file_t *f = syscall64_get_file((int)fd);
  if (!f || !f->node || !f->node->ops || !f->node->ops->ioctl)
    return k_sys_enosys;
  return (uint64_t)f->node->ops->ioctl(f->node, (uint32_t)cmd,
                                       (void *)(uintptr_t)arg);
}

static uint64_t sys_exit64(uint64_t code, uint64_t, uint64_t, uint64_t,
                           uint64_t, uint64_t) {
  serial_write_string("[K64] task exited with code ");
  serial_printf("%d", (int)code);
  serial_write_string("\r\n");
  vga_puts_k64("[K64] task exited\n");
  
  if (auto *t = task64_current()) {
    for (int i = 3; i < MAX_FILES_PER_PROCESS; ++i) {
      if (t->files[i]) {
        sys_close64((uint64_t)i, 0, 0, 0, 0, 0);
      }
    }
    t->exit_code = (int)code;
    t->state = TASK64_ZOMBIE;
    if (t->parent_id) {
      if (auto *parent = task64_find_by_id(t->parent_id)) {
        parent->sig_pending |= (1ULL << (17 - 1));
      }
    }
  }
  task64_yield();
  
  /* Should never return, but if it does, halt safely with IRQs enabled */
  for (;;) {
    asm volatile("sti; hlt");
  }
}

static uint64_t sys_yield64(uint64_t, uint64_t, uint64_t, uint64_t,
                            uint64_t, uint64_t) {
  task64_yield();
  return 0;
}

static uint64_t sys_sleep64(uint64_t ms, uint64_t, uint64_t, uint64_t,
                            uint64_t, uint64_t) {
  if (ms > 0)
    sleep((uint32_t)ms);
  else
    task64_yield();
  return 0;
}

static uint64_t sys_get_time64(uint64_t, uint64_t, uint64_t, uint64_t,
                               uint64_t, uint64_t) {
  struct datetime t;
  time_get_local(&t);
  return (uint64_t)((t.hour << 16) | (t.minute << 8) | t.second);
}

static void init_syscall_table(void) {
  for (int i = 0; i < k_syscall_table_size; ++i)
    g_syscall_table[i] = nullptr;

  register_syscall(SYS_WRITE, sys_write64);
  register_syscall(SYS_READ, sys_read64);
  register_syscall(SYS_OPEN, sys_open64);
  register_syscall(SYS_CLOSE, sys_close64);
  register_syscall(SYS_IOCTL, sys_ioctl64);
  register_syscall(SYS_EXIT, sys_exit64);
  register_syscall(SYS_YIELD, sys_yield64);
  register_syscall(SYS_SLEEP, sys_sleep64);
  register_syscall(SYS_GET_TIME, sys_get_time64);
}

void syscall64_init(void) {
  /* Enable SCE in EFER */
  uint32_t lo = 0, hi = 0;
  rdmsr(MSR_EFER, &lo, &hi);
  lo |= 1u; /* SCE */
  wrmsr(MSR_EFER, lo, hi);

  /*
   * STAR MSR Layout for SYSCALL/SYSRET (Bits 63:32):
   * 
   * [47:32] = Target CS for SYSCALL (kernel code).
   *           SYSCALL loads CS from STAR[47:32] and SS from STAR[47:32] + 8.
   *           We want CS=0x08 (kernel code), SS=0x10 (kernel data).
   *           So STAR[47:32] = 0x0008.
   *
   * [63:48] = Target CS/SS for SYSRET (user code/data).
   *           SYSRET computes CS as STAR[63:48] + 16, and SS as STAR[63:48] + 8.
   *           We want CS=0x23 (user code, RPL=3) and SS=0x1B (user data, RPL=3).
   *           Solving: CS base = 0x23 - 16 = 0x13.
   *                    SS base = 0x1B - 8 = 0x13.
   *           So STAR[63:48] = 0x0013.
   *
   * Combining: 0x0013 (high 16 bits) and 0x0008 (low 16 bits) -> 0x00130008.
   */
  uint64_t star = ((uint64_t)0x0013 << 48) | ((uint64_t)0x0008 << 32);
  wrmsr(MSR_STAR, (uint32_t)(star & 0xFFFFFFFFu),
        (uint32_t)(star >> 32));

  /* LSTAR: syscall entry point */
  uint64_t lstar = (uint64_t)(unsigned long long)syscall64_entry;
  wrmsr(MSR_LSTAR, (uint32_t)(lstar & 0xFFFFFFFFu),
        (uint32_t)(lstar >> 32));

  /* FMASK: clear IF (bit 9) on entry to disable interrupts during entry setup */
  wrmsr(MSR_FMASK, 1u << 9, 0);

  init_syscall_table();
}

void syscall64_set_linux_abi(int enabled) { g_linux_abi_mode = enabled ? 1 : 0; }

syscall64_state_t *syscall64_get_state(void) { return g_syscall_state; }

file_t *syscall64_get_file(int fd) {
  if (fd < 0 || fd >= MAX_FILES_PER_PROCESS)
    return nullptr;
  task64_t *t = task64_current();
  if (t)
    return t->files[fd];
  return g_boot_files[fd];
}

void syscall64_set_file(int fd, file_t *f) {
  if (fd < 0 || fd >= MAX_FILES_PER_PROCESS)
    return;
  task64_t *t = task64_current();
  if (t)
    t->files[fd] = f;
  else
    g_boot_files[fd] = f;
}

uint8_t syscall64_get_fd_flags(int fd) {
  if (fd < 0 || fd >= MAX_FILES_PER_PROCESS)
    return 0;
  task64_t *t = task64_current();
  if (t)
    return t->fd_flags[fd];
  return g_boot_fd_flags[fd];
}

void syscall64_set_fd_flags(int fd, uint8_t flags) {
  if (fd < 0 || fd >= MAX_FILES_PER_PROCESS)
    return;
  task64_t *t = task64_current();
  if (t)
    t->fd_flags[fd] = flags;
  else
    g_boot_fd_flags[fd] = flags;
}

extern "C" uint64_t syscall64_dispatch_native(syscall64_state_t *state, uint64_t num, uint64_t a1,
                                   uint64_t a2, uint64_t a3, uint64_t a4,
                                   uint64_t a5, uint64_t a6) {
  (void)state;
  if (num >= (uint64_t)k_syscall_table_size || !g_syscall_table[num]) {
    serial_write_string("[K64] unknown syscall\r\n");
    vga_puts_k64("[K64] unknown syscall\n");
    return k_sys_enosys;
  }
  return g_syscall_table[num](a1, a2, a3, a4, a5, a6);
}

extern "C" uint64_t syscall64_dispatch(syscall64_state_t *state, uint64_t num, uint64_t a1, uint64_t a2,
                            uint64_t a3, uint64_t a4, uint64_t a5,
                            uint64_t a6) {
  if (g_linux_abi_mode) {
    return (uint64_t)linux_syscall_dispatch_x86_64((void *)state, num, a1, a2, a3,
                                                   a4, a5, a6);
  }

  return syscall64_dispatch_native(state, num, a1, a2, a3, a4, a5, a6);
}

extern "C" void __syscall_handler(syscall64_state_t *state) {
  if (!state)
    return;
  if (auto *t = task64_current()) {
    t->rsp = (uint64_t)(uintptr_t)state - 8;
#if 0
    if (t->id > 2) {
      serial_write_string("[K64-raw] Task ");
      serial_write_hex(t->id);
      serial_write_string(" syscall rax=");
      serial_write_hex(state->rax);
      serial_write_string("\r\n");
    }
#endif
  }
  g_syscall_state = state;
  state->rax = syscall64_dispatch(state, state->rax, state->rdi, state->rsi,
                                  state->rdx, state->r10, state->r8, state->r9);
  g_syscall_state = nullptr;
}
