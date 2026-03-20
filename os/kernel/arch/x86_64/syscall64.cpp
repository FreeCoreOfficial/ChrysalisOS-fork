#include "syscall64.h"
#include "../../hardware/msr.h"
#include "../../include/chrysalis/syscall_nums.h"
#include "../../drivers/serial.h"
#include "../../fs/vfs/fs_ops.h"
#include "../../fs/vfs/vfs.h"
#include "../../linux_compat/linux_syscall_x86_64.h"
#include "../../mem/kmalloc.h"
#include "../../sched/task64.h"
#include "../../time/clock.h"
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

static constexpr uint64_t k_sys_enosys = (uint64_t)-38;
static constexpr int k_syscall_table_size = 128;
static syscall64_fn g_syscall_table[k_syscall_table_size];
static file_t *g_files[MAX_FILES_PER_PROCESS];
static int g_linux_abi_mode = 0;
static syscall64_state_t *g_syscall_state = nullptr;

static void register_syscall(uint32_t num, syscall64_fn fn) {
  if (num < (uint32_t)k_syscall_table_size)
    g_syscall_table[num] = fn;
}

static uint64_t sys_write64(uint64_t fd, uint64_t buf, uint64_t size,
                            uint64_t, uint64_t, uint64_t) {
  if (!buf || size == 0)
    return 0;
  const char *s = (const char *)(uintptr_t)buf;
  if (fd == 1 || fd == 2 || fd == 0) {
    for (uint64_t i = 0; i < size; ++i) {
      serial_write(s[i]);
    }
    return size;
  }
  if (fd >= (uint64_t)MAX_FILES_PER_PROCESS)
    return k_sys_enosys;
  file_t *f = g_files[fd];
  if (!f || !f->node || !f->node->ops || !f->node->ops->write)
    return k_sys_enosys;
  int bytes = f->node->ops->write(f->node, f->offset,
                                  (const uint8_t *)s, (uint32_t)size);
  if (bytes > 0)
    f->offset += (uint32_t)bytes;
  return (uint64_t)bytes;
}

static uint64_t sys_read64(uint64_t fd, uint64_t buf, uint64_t size,
                           uint64_t, uint64_t, uint64_t) {
  if (!buf || size == 0)
    return 0;
  if (fd == 0) {
    char *out = (char *)(uintptr_t)buf;
    for (uint64_t i = 0; i < size; ++i) {
      out[i] = serial_read();
    }
    return size;
  }
  if (fd >= (uint64_t)MAX_FILES_PER_PROCESS)
    return k_sys_enosys;
  file_t *f = g_files[fd];
  if (!f || !f->node || !f->node->ops || !f->node->ops->read)
    return k_sys_enosys;
  int bytes =
      f->node->ops->read(f->node, f->offset, (uint8_t *)buf, (uint32_t)size);
  if (bytes > 0)
    f->offset += (uint32_t)bytes;
  return (uint64_t)bytes;
}

static uint64_t sys_open64(uint64_t path_ptr, uint64_t flags, uint64_t,
                           uint64_t, uint64_t, uint64_t) {
  if (!path_ptr)
    return k_sys_enosys;
  const char *path = (const char *)(uintptr_t)path_ptr;
  vnode_t *node = vfs_resolve(path);
  if (!node)
    return k_sys_enosys;
  if (node->ops && node->ops->open) {
    if (node->ops->open(node) < 0)
      return k_sys_enosys;
  }
  for (int i = 0; i < MAX_FILES_PER_PROCESS; ++i) {
    if (!g_files[i]) {
      file_t *f = (file_t *)kmalloc(sizeof(file_t));
      if (!f)
        return k_sys_enosys;
      f->node = node;
      f->offset = 0;
      f->flags = (int)flags;
      g_files[i] = f;
      return (uint64_t)i;
    }
  }
  return k_sys_enosys;
}

static uint64_t sys_close64(uint64_t fd, uint64_t, uint64_t, uint64_t,
                            uint64_t, uint64_t) {
  if (fd >= (uint64_t)MAX_FILES_PER_PROCESS)
    return k_sys_enosys;
  file_t *f = g_files[fd];
  if (!f)
    return k_sys_enosys;
  if (f->node && f->node->ops && f->node->ops->close)
    f->node->ops->close(f->node);
  kfree(f);
  g_files[fd] = nullptr;
  return 0;
}

static uint64_t sys_ioctl64(uint64_t fd, uint64_t cmd, uint64_t arg,
                            uint64_t, uint64_t, uint64_t) {
  if (fd >= (uint64_t)MAX_FILES_PER_PROCESS)
    return k_sys_enosys;
  file_t *f = g_files[fd];
  if (!f || !f->node || !f->node->ops || !f->node->ops->ioctl)
    return k_sys_enosys;
  return (uint64_t)f->node->ops->ioctl(f->node, (uint32_t)cmd,
                                       (void *)(uintptr_t)arg);
}

static uint64_t sys_exit64(uint64_t code, uint64_t, uint64_t, uint64_t,
                           uint64_t, uint64_t) {
  serial_write_string("[K64] sys_exit ");
  serial_printf("%d", (int)code);
  serial_write_string("\r\n");
  for (;;) {
    asm volatile("hlt");
  }
}

static uint64_t sys_yield64(uint64_t, uint64_t, uint64_t, uint64_t,
                            uint64_t, uint64_t) {
  task64_yield();
  return 0;
}

static uint64_t sys_sleep64(uint64_t ms, uint64_t, uint64_t, uint64_t,
                            uint64_t, uint64_t) {
  volatile uint64_t spins = ms * 10000ULL;
  while (spins--)
    asm volatile("");
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

  /* STAR: kernel CS = 0x08, user SS = 0x18, user CS = 0x20 */
  uint64_t star = ((uint64_t)0x0010 << 48) | ((uint64_t)0x0008 << 32);
  wrmsr(MSR_STAR, (uint32_t)(star & 0xFFFFFFFFu),
        (uint32_t)(star >> 32));

  /* LSTAR: syscall entry point */
  uint64_t lstar = (uint64_t)(unsigned long long)syscall64_entry;
  wrmsr(MSR_LSTAR, (uint32_t)(lstar & 0xFFFFFFFFu),
        (uint32_t)(lstar >> 32));

  /* FMASK: clear IF on entry */
  wrmsr(MSR_FMASK, 1u << 9, 0);

  init_syscall_table();
}

void syscall64_set_linux_abi(int enabled) { g_linux_abi_mode = enabled ? 1 : 0; }

syscall64_state_t *syscall64_get_state(void) { return g_syscall_state; }

file_t *syscall64_get_file(int fd) {
  if (fd < 0 || fd >= MAX_FILES_PER_PROCESS)
    return nullptr;
  return g_files[fd];
}

void syscall64_set_file(int fd, file_t *f) {
  if (fd < 0 || fd >= MAX_FILES_PER_PROCESS)
    return;
  g_files[fd] = f;
}

uint64_t syscall64_dispatch(uint64_t num, uint64_t a1, uint64_t a2,
                            uint64_t a3, uint64_t a4, uint64_t a5,
                            uint64_t a6) {
  if (g_linux_abi_mode) {
    return (uint64_t)linux_syscall_dispatch_x86_64(num, a1, a2, a3, a4, a5,
                                                   a6);
  }
  if (num >= (uint64_t)k_syscall_table_size)
    return k_sys_enosys;
  syscall64_fn fn = g_syscall_table[num];
  if (!fn)
    return k_sys_enosys;
  return fn(a1, a2, a3, a4, a5, a6);
}

extern "C" void __syscall_handler(syscall64_state_t *state) {
  if (!state)
    return;
  if (auto *t = task64_current()) {
    t->rsp = (uint64_t)(uintptr_t)state - 8;
  }
  g_syscall_state = state;
  state->rax = syscall64_dispatch(state->rax, state->rdi, state->rsi,
                                  state->rdx, state->r10, state->r8, state->r9);
  g_syscall_state = nullptr;
}
