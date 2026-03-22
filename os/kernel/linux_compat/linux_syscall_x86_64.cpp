#include "linux_syscall_x86_64.h"
#include "linux_abi.h"

#include "../include/chrysalis/syscall_nums.h"
#include "../include/task.h"
#include "../sched/task64.h"
#include "../string.h"
#include "../time/clock.h"
#include "../mem/user64_vm.h"
#include "../mem/kmalloc.h"
#include "../arch/x86_64/syscall64.h"
#include "../fs/pipe/pipe.h"
#include "../fs/vfs/vfs.h"
#include "../fs/vfs/fs_ops.h"
#include "../arch/x86_64/syscall64.h"


#define LINUX_EFAULT 14
#define LINUX_EAGAIN 11
#define LINUX_ENOMEM 12
#define LINUX_EINVAL 22
#define LINUX_ENOSYS 38
#define LINUX_EEXIST 17
#define LINUX_ENOENT 2
#define LINUX_EBADF 9
#define LINUX_ESRCH 3
#define LINUX_ETIMEDOUT 110

#if defined(__x86_64__)


/* Minimal x86_64 Linux syscall numbers (subset) */
#define LINUX_NR_read 0
#define LINUX_NR_write 1
#define LINUX_NR_open 2
#define LINUX_NR_close 3
#define LINUX_NR_stat 4
#define LINUX_NR_fstat 5
#define LINUX_NR_lstat 6
#define LINUX_NR_lseek 8
#define LINUX_NR_mmap 9
#define LINUX_NR_mprotect 10
#define LINUX_NR_munmap 11
#define LINUX_NR_brk 12
#define LINUX_NR_poll 7
#define LINUX_NR_pipe 22
#define LINUX_NR_sched_yield 24
#define LINUX_NR_nanosleep 35
#define LINUX_NR_getpid 39
#define LINUX_NR_uname 63
#define LINUX_NR_gettimeofday 96
#define LINUX_NR_sysinfo 99
#define LINUX_NR_getuid 102
#define LINUX_NR_getgid 104
#define LINUX_NR_getppid 110
#define LINUX_NR_time 201
#define LINUX_NR_clock_gettime 228
#define LINUX_NR_exit 60
#define LINUX_NR_exit_group 231
#define LINUX_NR_openat 257
#define LINUX_NR_futex 202
#define LINUX_NR_rt_sigaction 13
#define LINUX_NR_rt_sigprocmask 14
#define LINUX_NR_rt_sigreturn 15
#define LINUX_NR_kill 62
#define LINUX_NR_tkill 200
#define LINUX_NR_epoll_create 213
#define LINUX_NR_epoll_ctl 233
#define LINUX_NR_epoll_wait 232
#define LINUX_NR_epoll_pwait 281
#define LINUX_NR_epoll_create1 291
#define LINUX_NR_pipe2 293
#define LINUX_NR_tgkill 234
#define LINUX_NR_clone 56
#define LINUX_NR_arch_prctl 158
#define LINUX_NR_gettid 186
#define LINUX_NR_set_tid_address 218

#define FUTEX_WAIT 0
#define FUTEX_WAKE 1
#define FUTEX_WAKE_OP 5
#define FUTEX_WAIT_BITSET 9
#define FUTEX_WAKE_BITSET 10
#define FUTEX_PRIVATE_FLAG 128
#define FUTEX_CLOCK_REALTIME 256
#define FUTEX_CMD_MASK ~(FUTEX_PRIVATE_FLAG | FUTEX_CLOCK_REALTIME)
#define FUTEX_BITSET_MATCH_ANY 0xFFFFFFFFu
#define FUTEX_OP_SET 0
#define FUTEX_OP_ADD 1
#define FUTEX_OP_OR 2
#define FUTEX_OP_ANDN 3
#define FUTEX_OP_XOR 4
#define FUTEX_OP_OPARG_SHIFT 8
#define FUTEX_OP_CMP_EQ 0
#define FUTEX_OP_CMP_NE 1
#define FUTEX_OP_CMP_LT 2
#define FUTEX_OP_CMP_LE 3
#define FUTEX_OP_CMP_GT 4
#define FUTEX_OP_CMP_GE 5

#define EPOLL_CTL_ADD 1
#define EPOLL_CTL_DEL 2
#define EPOLL_CTL_MOD 3
#define EPOLLIN 0x001
#define EPOLLOUT 0x004
#define EPOLLET 0x80000000u
#define EPOLLONESHOT (1u << 30)

#define POLLIN 0x001
#define POLLOUT 0x004

#define PROT_READ 0x1
#define PROT_WRITE 0x2
#define PROT_EXEC 0x4
#define MAP_FIXED 0x10
#define MAP_ANONYMOUS 0x20

#define SIG_BLOCK 0
#define SIG_UNBLOCK 1
#define SIG_SETMASK 2
#define SIGKILL 9
#define SIGTERM 15
#define SIGCHLD 17
#define SIGSTOP 19
#define SIGWINCH 28

#define CLONE_VM 0x00000100
#define CLONE_FS 0x00000200
#define CLONE_FILES 0x00000400
#define CLONE_SIGHAND 0x00000800
#define CLONE_THREAD 0x00010000
#define CLONE_SETTLS 0x00080000
#define CLONE_PARENT_SETTID 0x00100000
#define CLONE_CHILD_CLEARTID 0x00200000
#define CLONE_CHILD_SETTID 0x01000000

#define ARCH_SET_FS 0x1002
#define ARCH_GET_FS 0x1003

typedef struct linux_sigaction64 {
  uint64_t handler;
  uint64_t flags;
  uint64_t restorer;
  uint64_t mask;
} linux_sigaction64_t;

typedef struct linux_epoll_event {
  uint32_t events;
  uint64_t data;
} linux_epoll_event_t;

typedef struct linux_pollfd {
  int32_t fd;
  int16_t events;
  int16_t revents;
} linux_pollfd_t;

typedef struct linux_stat64 {
  uint64_t st_dev;
  uint64_t st_ino;
  uint64_t st_nlink;
  uint32_t st_mode;
  uint32_t st_uid;
  uint32_t st_gid;
  uint32_t __pad0;
  uint64_t st_rdev;
  int64_t st_size;
  int64_t st_blksize;
  int64_t st_blocks;
  struct linux_timespec64 st_atim;
  struct linux_timespec64 st_mtim;
  struct linux_timespec64 st_ctim;
  int64_t __unused[3];
} linux_stat64_t;

typedef struct futex_waiter64 {
  uint64_t key;
  task64_t *task;
  int in_use;
  int woken;
} futex_waiter64_t;

#define FUTEX64_MAX_WAITERS 128
static futex_waiter64_t g_futex64_waiters[FUTEX64_MAX_WAITERS];

typedef struct epoll_item64 {
  int used;
  int fd;
  uint32_t events;
  uint64_t data;
  uint32_t last_revents;
  int oneshot;
  int disabled;
} epoll_item64_t;

typedef struct epoll_obj64 {
  epoll_item64_t items[64];
} epoll_obj64_t;

static int linux_copy_to_user(void *dst, const void *src, uint32_t len) {
  if (!dst || !src || len == 0)
    return -LINUX_EFAULT;
  memcpy(dst, src, len);
  return 0;
}

static int linux_mmap_load_file(uint64_t addr, uint64_t len, int fd,
                                uint64_t file_off) {
  if (fd < 0 || fd >= MAX_FILES_PER_PROCESS)
    return -LINUX_EBADF;
  file_t *f = syscall64_get_file(fd);
  if (!f || !f->node || !f->node->ops || !f->node->ops->read)
    return -LINUX_EBADF;

  uint64_t remaining = len;
  uint64_t dst = addr;
  uint64_t off = file_off;
  uint8_t tmp[256];
  while (remaining > 0) {
    uint32_t chunk =
        remaining > sizeof(tmp) ? (uint32_t)sizeof(tmp) : (uint32_t)remaining;
    int r = f->node->ops->read(f->node, (uint32_t)off, tmp, chunk);
    if (r <= 0)
      break;
    memcpy((void *)(uintptr_t)dst, tmp, (uint32_t)r);
    dst += (uint32_t)r;
    off += (uint32_t)r;
    remaining -= (uint32_t)r;
    if ((uint32_t)r < chunk)
      break;
  }
  return 0;
}

static inline uint64_t linux_now_ms(void) {
  struct datetime t;
  time_get_utc(&t);
  return (uint64_t)t.second * 1000ULL +
         (uint64_t)t.minute * 60000ULL +
         (uint64_t)t.hour * 3600000ULL;
}

static futex_waiter64_t *futex64_alloc_waiter(void) {
  for (int i = 0; i < FUTEX64_MAX_WAITERS; i++) {
    if (!g_futex64_waiters[i].in_use) {
      g_futex64_waiters[i].in_use = 1;
      g_futex64_waiters[i].woken = 0;
      g_futex64_waiters[i].task = nullptr;
      g_futex64_waiters[i].key = 0;
      return &g_futex64_waiters[i];
    }
  }
  return nullptr;
}

static void futex64_free_waiter(futex_waiter64_t *w) {
  if (!w)
    return;
  w->in_use = 0;
  w->task = nullptr;
  w->key = 0;
  w->woken = 0;
}

static inline uint64_t futex64_make_key(task64_t *t, uint64_t uaddr) {
  uint64_t tid = t ? t->id : 0;
  return (tid << 32) ^ uaddr;
}

static int futex64_wait_impl(uint64_t uaddr, uint32_t expected,
                             uint64_t timeout_ms) {
  uint32_t *ptr = (uint32_t *)(uintptr_t)uaddr;
  if (!ptr)
    return -LINUX_EFAULT;
  if (__atomic_load_n(ptr, __ATOMIC_SEQ_CST) != expected)
    return -LINUX_EAGAIN;

  task64_t *t = task64_current();
  if (!t)
    return -LINUX_EINVAL;

  futex_waiter64_t *w = futex64_alloc_waiter();
  if (!w)
    return -LINUX_ENOMEM;

  w->task = t;
  w->key = futex64_make_key(t, uaddr);

  uint64_t start_ms = linux_now_ms();
  while (!w->woken) {
    if (timeout_ms > 0 && (linux_now_ms() - start_ms) >= timeout_ms) {
      futex64_free_waiter(w);
      return -LINUX_ETIMEDOUT;
    }
    syscall64_dispatch_native(SYS_SLEEP, 1, 0, 0, 0, 0, 0);
  }
  futex64_free_waiter(w);
  return 0;
}

static int futex64_wake_impl(uint64_t uaddr, int max_to_wake) {
  task64_t *t = task64_current();
  if (!t)
    return 0;
  uint64_t key = futex64_make_key(t, uaddr);
  int woken = 0;
  for (int i = 0; i < FUTEX64_MAX_WAITERS && woken < max_to_wake; i++) {
    futex_waiter64_t *w = &g_futex64_waiters[i];
    if (w->in_use && w->key == key && w->task) {
      w->woken = 1;
      w->task->state = TASK64_READY;
      woken++;
    }
  }
  return woken;
}

static int futex64_wake_op(uint64_t uaddr1, int max_to_wake1, uint64_t uaddr2,
                           int max_to_wake2, uint32_t op_encoded) {
  uint32_t op = (op_encoded >> 28) & 0xF;
  uint32_t op_arg = (op_encoded >> 24) & 0xF;
  uint32_t cmp = (op_encoded >> 12) & 0xFFF;
  uint32_t cmp_arg = op_encoded & 0xFFF;
  uint32_t *p2 = (uint32_t *)(uintptr_t)uaddr2;
  uint32_t old_val = 0;

  if (op & FUTEX_OP_OPARG_SHIFT) {
    op_arg = 1u << op_arg;
    op &= ~FUTEX_OP_OPARG_SHIFT;
  }

  switch (op) {
  case FUTEX_OP_SET:
    old_val = __atomic_exchange_n(p2, op_arg, __ATOMIC_SEQ_CST);
    break;
  case FUTEX_OP_ADD:
    old_val = __atomic_fetch_add(p2, op_arg, __ATOMIC_SEQ_CST);
    break;
  case FUTEX_OP_OR:
    old_val = __atomic_fetch_or(p2, op_arg, __ATOMIC_SEQ_CST);
    break;
  case FUTEX_OP_ANDN:
    old_val = __atomic_fetch_nand(p2, op_arg, __ATOMIC_SEQ_CST);
    break;
  case FUTEX_OP_XOR:
    old_val = __atomic_fetch_xor(p2, op_arg, __ATOMIC_SEQ_CST);
    break;
  default:
    return -LINUX_EINVAL;
  }

  int ret1 = futex64_wake_impl(uaddr1, max_to_wake1);
  if (ret1 < 0)
    return ret1;

  int cmp_ok = 0;
  switch (cmp) {
  case FUTEX_OP_CMP_EQ:
    cmp_ok = (old_val == cmp_arg);
    break;
  case FUTEX_OP_CMP_NE:
    cmp_ok = (old_val != cmp_arg);
    break;
  case FUTEX_OP_CMP_LT:
    cmp_ok = (old_val < cmp_arg);
    break;
  case FUTEX_OP_CMP_LE:
    cmp_ok = (old_val <= cmp_arg);
    break;
  case FUTEX_OP_CMP_GT:
    cmp_ok = (old_val > cmp_arg);
    break;
  case FUTEX_OP_CMP_GE:
    cmp_ok = (old_val >= cmp_arg);
    break;
  default:
    return -LINUX_EINVAL;
  }

  int ret2 = 0;
  if (cmp_ok)
    ret2 = futex64_wake_impl(uaddr2, max_to_wake2);
  return ret1 + ret2;
}

static inline epoll_obj64_t *epoll64_get(task64_t *t, int fd) {
  if (!t)
    return nullptr;
  if (fd < TASK_LINUX_EPOLL_FD_BASE ||
      fd >= TASK_LINUX_EPOLL_FD_BASE + TASK_LINUX_EPOLL_MAX)
    return nullptr;
  return (epoll_obj64_t *)t->epoll_table[fd - TASK_LINUX_EPOLL_FD_BASE];
}

static int epoll64_alloc_fd(task64_t *t) {
  if (!t)
    return -1;
  for (int i = 0; i < TASK_LINUX_EPOLL_MAX; i++) {
    if (!t->epoll_table[i]) {
      epoll_obj64_t *ep = (epoll_obj64_t *)kmalloc(sizeof(epoll_obj64_t));
      if (!ep)
        return -1;
      memset(ep, 0, sizeof(*ep));
      t->epoll_table[i] = ep;
      return TASK_LINUX_EPOLL_FD_BASE + i;
    }
  }
  return -1;
}

static int epoll64_close(task64_t *t, int fd) {
  if (!t)
    return -LINUX_EBADF;
  if (fd < TASK_LINUX_EPOLL_FD_BASE ||
      fd >= TASK_LINUX_EPOLL_FD_BASE + TASK_LINUX_EPOLL_MAX)
    return -LINUX_EBADF;
  int idx = fd - TASK_LINUX_EPOLL_FD_BASE;
  epoll_obj64_t *ep = (epoll_obj64_t *)t->epoll_table[idx];
  if (!ep)
    return -LINUX_EBADF;
  kfree(ep);
  t->epoll_table[idx] = nullptr;
  return 0;
}

static uint32_t epoll64_compute_revents(epoll_item64_t *it) {
  if (!it)
    return 0;
  if (it->fd < 0 || it->fd >= MAX_FILES_PER_PROCESS)
    return 0;
  file_t *f = syscall64_get_file(it->fd);
  if (!f || !f->node || !f->node->ops)
    return 0;
  if (f->node->ops->poll)
    return f->node->ops->poll(f->node, it->events);
  return it->events & (EPOLLIN | EPOLLOUT);
}

static void signal64_dispatch(task64_t *t, syscall64_state_t *state) {
  if (!t || !state)
    return;
  if (t->sig_active)
    return;
  uint64_t pending = t->sig_pending & ~t->sig_mask;
  if (!pending)
    return;
  int sig = 1;
  for (int i = 0; i < 64; i++) {
    if (pending & (1ULL << i)) {
      sig = i + 1;
      break;
    }
  }
  t->sig_pending &= ~(1ULL << (sig - 1));
  if (sig == SIGKILL || sig == SIGSTOP) {
    return;
  }
  auto *act = &t->sig_actions[sig - 1];
  if (!act->handler || act->handler == (void (*)(int))1) {
    if (sig == SIGCHLD || sig == SIGWINCH)
      return;
    return;
  }

  uint64_t tramp = user64_get_sigtramp();
  if (!tramp)
    return;

  t->sig_saved_rip = state->rcx;
  t->sig_saved_rsp = t->gs.user_stack;
  t->sig_active = 1;

  uint64_t new_rsp = t->gs.user_stack;
  new_rsp -= 8;
  *(uint64_t *)(uintptr_t)new_rsp = tramp;
  t->gs.user_stack = new_rsp;

  state->rcx = (uint64_t)(uintptr_t)act->handler;
}

static void linux_fill_utsname(struct linux_utsname *u) {
  if (!u)
    return;
  strncpy(u->sysname, "ChrysalisOS", sizeof(u->sysname) - 1);
  strncpy(u->nodename, "chrysalis", sizeof(u->nodename) - 1);
#ifdef CHRYVER
  strncpy(u->release, CHRYVER, sizeof(u->release) - 1);
#else
  strncpy(u->release, "chrysver-unknown", sizeof(u->release) - 1);
#endif
  strncpy(u->version, __DATE__ " " __TIME__, sizeof(u->version) - 1);
  strncpy(u->machine, "x86_64", sizeof(u->machine) - 1);
  strncpy(u->domainname, "(none)", sizeof(u->domainname) - 1);
  u->sysname[sizeof(u->sysname) - 1] = 0;
  u->nodename[sizeof(u->nodename) - 1] = 0;
  u->release[sizeof(u->release) - 1] = 0;
  u->version[sizeof(u->version) - 1] = 0;
  u->machine[sizeof(u->machine) - 1] = 0;
  u->domainname[sizeof(u->domainname) - 1] = 0;
}

static int linux_sys_uname(uint64_t a1) {
  struct linux_utsname u;
  linux_fill_utsname(&u);
  return linux_copy_to_user((void *)(uintptr_t)a1, &u, sizeof(u));
}

static uint64_t days_before_year(int year) {
  uint64_t y = (uint64_t)year;
  uint64_t y_prev = y - 1;
  return 365ULL * y_prev + y_prev / 4 - y_prev / 100 + y_prev / 400;
}

static uint64_t days_before_month(int year, int month) {
  static const int days_norm[] = {0, 31, 59, 90, 120, 151, 181,
                                  212, 243, 273, 304, 334};
  uint64_t days = days_norm[month - 1];
  bool leap = ((year % 4) == 0 && (year % 100) != 0) || (year % 400) == 0;
  if (leap && month > 2)
    days++;
  return days;
}

static uint64_t linux_epoch_from_datetime(const struct datetime *t) {
  if (!t || t->year < 1970 || t->month < 1 || t->month > 12 || t->day < 1)
    return 0;
  uint64_t days_1970 = days_before_year(1970);
  uint64_t days = days_before_year(t->year) - days_1970;
  days += days_before_month(t->year, t->month);
  days += (uint64_t)(t->day - 1);
  uint64_t sec = days * 86400ULL;
  sec += (uint64_t)t->hour * 3600ULL;
  sec += (uint64_t)t->minute * 60ULL;
  sec += (uint64_t)t->second;
  return sec;
}

static int linux_sys_gettimeofday(uint64_t a1) {
  if (a1 == 0)
    return 0;
  struct datetime t;
  time_get_utc(&t);
  struct linux_timeval64 tv;
  tv.tv_sec = (int64_t)linux_epoch_from_datetime(&t);
  tv.tv_usec = 0;
  return linux_copy_to_user((void *)(uintptr_t)a1, &tv, sizeof(tv));
}

static int linux_sys_time(uint64_t a1) {
  struct datetime t;
  time_get_utc(&t);
  uint64_t now = linux_epoch_from_datetime(&t);
  if (a1) {
    *(uint64_t *)(uintptr_t)a1 = now;
  }
  return (int)now;
}

static uint16_t linux_mode_from_vnode(vnode_t *n) {
  if (!n)
    return 0;
  switch (n->type) {
  case VNODE_DIR:
    return 0040000 | 0755;
  case VNODE_DEV:
    return 0020000 | 0666;
  case VNODE_FILE:
  default:
    return 0100000 | 0644;
  }
}

static int linux_fill_stat64(vnode_t *node, linux_stat64_t *st) {
  if (!node || !st)
    return -LINUX_EINVAL;
  memset(st, 0, sizeof(*st));
  st->st_mode = linux_mode_from_vnode(node);
  st->st_nlink = 1;
  st->st_uid = 0;
  st->st_gid = 0;
  uint64_t size = node->size;
  st->st_size = (int64_t)size;
  st->st_blksize = 512;
  st->st_blocks = (int64_t)((size + 511ULL) / 512ULL);
  struct datetime t;
  time_get_utc(&t);
  uint64_t epoch = linux_epoch_from_datetime(&t);
  st->st_atim.tv_sec = (int64_t)epoch;
  st->st_mtim.tv_sec = (int64_t)epoch;
  st->st_ctim.tv_sec = (int64_t)epoch;
  return 0;
}

static int linux_sys_stat64(const char *path, linux_stat64_t *out) {
  if (!path || !out)
    return -LINUX_EFAULT;
  vnode_t *node = vfs_resolve(path);
  if (!node)
    return -LINUX_ENOENT;
  linux_stat64_t st;
  linux_fill_stat64(node, &st);
  return linux_copy_to_user(out, &st, sizeof(st));
}

static int linux_sys_fstat64(int fd, linux_stat64_t *out) {
  if (!out)
    return -LINUX_EFAULT;
  file_t *f = syscall64_get_file(fd);
  if (!f || !f->node)
    return -LINUX_EBADF;
  linux_stat64_t st;
  linux_fill_stat64(f->node, &st);
  return linux_copy_to_user(out, &st, sizeof(st));
}

static int linux_sys_clock_gettime(uint64_t clk_id, uint64_t out_ptr) {
  if (!out_ptr)
    return -LINUX_EFAULT;
  struct linux_timespec64 ts;
  if (clk_id == 0) {
    struct datetime t;
    time_get_utc(&t);
    ts.tv_sec = (int64_t)linux_epoch_from_datetime(&t);
    ts.tv_nsec = 0;
  } else {
    uint64_t ms = linux_now_ms();
    ts.tv_sec = (int64_t)(ms / 1000ULL);
    ts.tv_nsec = (int64_t)((ms % 1000ULL) * 1000000ULL);
  }
  return linux_copy_to_user((void *)(uintptr_t)out_ptr, &ts, sizeof(ts));
}

static int linux_sys_sysinfo(uint64_t a1) {
  if (!a1)
    return -LINUX_EFAULT;
  struct linux_sysinfo info;
  memset(&info, 0, sizeof(info));
  info.uptime = 0;
  info.mem_unit = 1;
  return linux_copy_to_user((void *)(uintptr_t)a1, &info, sizeof(info));
}

static int linux_sys_nanosleep(uint64_t a1) {
  if (!a1)
    return -LINUX_EFAULT;
  struct linux_timespec64 req;
  memcpy(&req, (const void *)(uintptr_t)a1, sizeof(req));
  if (req.tv_sec < 0 || req.tv_nsec < 0)
    return -LINUX_EINVAL;
  uint64_t ms = (uint64_t)req.tv_sec * 1000ULL +
                (uint64_t)req.tv_nsec / 1000000ULL;
  syscall64_dispatch_native(SYS_SLEEP, ms, 0, 0, 0, 0, 0);
  return 0;
}

static int linux_sys_arch_prctl(uint64_t code, uint64_t addr) {
  task64_t *t = task64_current();
  if (!t)
    return -LINUX_ESRCH;
  switch (code) {
  case ARCH_SET_FS:
    t->gs.fs_base = addr;
    return 0;
  case ARCH_GET_FS:
    if (!addr)
      return -LINUX_EFAULT;
    *(uint64_t *)(uintptr_t)addr = t->gs.fs_base;
    return 0;
  default:
    return -LINUX_EINVAL;
  }
}

static task64_t *task64_clone_current(uint64_t child_stack, uint64_t fs_base) {
  task64_t *parent = task64_current();
  if (!parent)
    return nullptr;

  task64_t *child = task64_create(parent->name, parent->entry, parent->arg);
  if (!child)
    return nullptr;

  child->user_brk_start = parent->user_brk_start;
  child->user_brk_end = parent->user_brk_end;
  child->user_mmap_base = parent->user_mmap_base;
  memcpy(child->vmas, parent->vmas, sizeof(parent->vmas));

  child->sig_pending = parent->sig_pending;
  child->sig_mask = parent->sig_mask;
  memcpy(child->sig_actions, parent->sig_actions, sizeof(parent->sig_actions));
  memcpy(child->epoll_table, parent->epoll_table, sizeof(parent->epoll_table));

  if (child_stack)
    child->gs.user_stack = child_stack;
  else
    child->gs.user_stack = parent->gs.user_stack;

  child->gs.fs_base = fs_base;

  uint64_t parent_top = parent->gs.kernel_stack;
  uint64_t parent_rsp = parent->rsp;
  if (parent_rsp == 0 || parent_rsp > parent_top)
    return child;

  uint64_t used = parent_top - parent_rsp;
  uint64_t child_top = child->gs.kernel_stack;
  uint64_t child_rsp = child_top - used;
  memcpy((void *)(uintptr_t)child_rsp, (const void *)(uintptr_t)parent_rsp,
         (size_t)used);
  child->rsp = child_rsp;

  syscall64_state_t *st =
      (syscall64_state_t *)(uintptr_t)(child_rsp + 8);
  st->rax = 0;
  return child;
}

int linux_syscall_dispatch_x86_64(uint64_t num, uint64_t a1, uint64_t a2,
                                  uint64_t a3, uint64_t a4, uint64_t a5,
                                  uint64_t a6) {
  (void)a4;
  (void)a5;
  (void)a6;

  signal64_dispatch(task64_current(), syscall64_get_state());

  switch (num) {
  case LINUX_NR_exit:
  case LINUX_NR_exit_group:
    return (int)syscall64_dispatch_native(SYS_EXIT, a1, 0, 0, 0, 0, 0);

  case LINUX_NR_read:
    return (int)syscall64_dispatch_native(SYS_READ, a1, a2, a3, 0, 0, 0);

  case LINUX_NR_write:
    return (int)syscall64_dispatch_native(SYS_WRITE, a1, a2, a3, 0, 0, 0);

  case LINUX_NR_open:
    return (int)syscall64_dispatch_native(SYS_OPEN, a1, a2, 0, 0, 0, 0);

  case LINUX_NR_stat:
    return linux_sys_stat64((const char *)(uintptr_t)a1,
                            (linux_stat64_t *)(uintptr_t)a2);

  case LINUX_NR_fstat:
    return linux_sys_fstat64((int)a1, (linux_stat64_t *)(uintptr_t)a2);

  case LINUX_NR_lstat:
    return linux_sys_stat64((const char *)(uintptr_t)a1,
                            (linux_stat64_t *)(uintptr_t)a2);

  case LINUX_NR_lseek: {
    int fd = (int)a1;
    int64_t off = (int64_t)a2;
    int whence = (int)a3;
    file_t *f = syscall64_get_file(fd);
    if (!f)
      return -LINUX_EBADF;
    int64_t new_off = 0;
    switch (whence) {
    case 0:
      new_off = off;
      break;
    case 1:
      new_off = (int64_t)f->offset + off;
      break;
    case 2:
      new_off = off;
      break;
    default:
      return -LINUX_EINVAL;
    }
    if (new_off < 0)
      return -LINUX_EINVAL;
    f->offset = (uint32_t)new_off;
    return (int)new_off;
  }

  case LINUX_NR_openat:
    return (int)syscall64_dispatch_native(SYS_OPEN, a2, a3, 0, 0, 0, 0);

  case LINUX_NR_close:
    if (a1 >= TASK_LINUX_EPOLL_FD_BASE)
      return epoll64_close(task64_current(), (int)a1);
    return (int)syscall64_dispatch_native(SYS_CLOSE, a1, 0, 0, 0, 0, 0);

  case LINUX_NR_pipe:
  case LINUX_NR_pipe2: {
    int32_t *fdp = (int32_t *)(uintptr_t)a1;
    if (!fdp)
      return -LINUX_EFAULT;
    if (num == LINUX_NR_pipe2 && a2 != 0)
      return -LINUX_EINVAL;
    vnode_t *rnode = NULL;
    vnode_t *wnode = NULL;
    if (pipe_create(&rnode, &wnode) < 0)
      return -LINUX_ENOMEM;
    int rfd = -1;
    int wfd = -1;
    for (int i = 0; i < MAX_FILES_PER_PROCESS; i++) {
      if (!syscall64_get_file(i)) {
        file_t *f = (file_t *)kmalloc(sizeof(file_t));
        if (!f)
          return -LINUX_ENOMEM;
        f->node = rnode;
        f->offset = 0;
        f->flags = 0;
        syscall64_set_file(i, f);
        rfd = i;
        break;
      }
    }
    for (int i = 0; i < MAX_FILES_PER_PROCESS; i++) {
      if (!syscall64_get_file(i)) {
        file_t *f = (file_t *)kmalloc(sizeof(file_t));
        if (!f)
          return -LINUX_ENOMEM;
        f->node = wnode;
        f->offset = 0;
        f->flags = 0;
        syscall64_set_file(i, f);
        wfd = i;
        break;
      }
    }
    if (rfd < 0 || wfd < 0)
      return -LINUX_ENOMEM;
    fdp[0] = rfd;
    fdp[1] = wfd;
    return 0;
  }

  case LINUX_NR_poll: {
    linux_pollfd_t *fds = (linux_pollfd_t *)(uintptr_t)a1;
    int nfds = (int)a2;
    int timeout = (int)a3;
    if (nfds < 0 || nfds > 256)
      return -LINUX_EINVAL;
    uint64_t start = linux_now_ms();
    while (1) {
      int ready = 0;
      for (int i = 0; i < nfds; i++) {
        linux_pollfd_t *pfd = &fds[i];
        pfd->revents = 0;
        if (pfd->fd < 0 || pfd->fd >= MAX_FILES_PER_PROCESS)
          continue;
        file_t *f = syscall64_get_file(pfd->fd);
        if (!f || !f->node || !f->node->ops)
          continue;
        uint32_t re = 0;
        if (f->node->ops->poll)
          re = f->node->ops->poll(f->node, (uint32_t)pfd->events);
        else {
          if ((pfd->events & POLLIN) && f->node->ops->read)
            re |= POLLIN;
          if ((pfd->events & POLLOUT) && f->node->ops->write)
            re |= POLLOUT;
        }
        pfd->revents = (int16_t)re;
        if (re)
          ready++;
      }
      if (ready || timeout == 0)
        return ready;
      if (timeout > 0 && (int)(linux_now_ms() - start) >= timeout)
        return 0;
      syscall64_dispatch_native(SYS_SLEEP, 1, 0, 0, 0, 0, 0);
    }
  }

  case LINUX_NR_futex: {
    int op = (int)a2;
    uint32_t cmd = (uint32_t)(op & FUTEX_CMD_MASK);
    uint64_t timeout_ms = 0;
    if (cmd == FUTEX_WAIT || cmd == FUTEX_WAIT_BITSET) {
      if (a4) {
        struct linux_timespec64 ts;
        memcpy(&ts, (const void *)(uintptr_t)a4, sizeof(ts));
        if (ts.tv_sec < 0 || ts.tv_nsec < 0)
          return -LINUX_EINVAL;
        timeout_ms = (uint64_t)ts.tv_sec * 1000ULL +
                     (uint64_t)ts.tv_nsec / 1000000ULL;
      }
    }
    switch (cmd) {
    case FUTEX_WAIT:
      return futex64_wait_impl(a1, (uint32_t)a3, timeout_ms);
    case FUTEX_WAKE:
      return futex64_wake_impl(a1, (int)a3);
    case FUTEX_WAIT_BITSET:
      return futex64_wait_impl(a1, (uint32_t)a3, timeout_ms);
    case FUTEX_WAKE_BITSET:
      return futex64_wake_impl(a1, (int)a3);
    case FUTEX_WAKE_OP:
      return futex64_wake_op(a1, (int)a3, a5, (int)a4, (uint32_t)a6);
    default:
      return -LINUX_ENOSYS;
    }
  }

  case LINUX_NR_epoll_create:
    if ((int)a1 <= 0)
      return -LINUX_EINVAL;
    return epoll64_alloc_fd(task64_current());

  case LINUX_NR_epoll_create1:
    return epoll64_alloc_fd(task64_current());

  case LINUX_NR_epoll_ctl: {
    epoll_obj64_t *ep = epoll64_get(task64_current(), (int)a1);
    if (!ep)
      return -LINUX_EBADF;
    int op = (int)a2;
    int fd = (int)a3;
    linux_epoll_event_t ev;
    if (op != EPOLL_CTL_DEL) {
      memcpy(&ev, (const void *)(uintptr_t)a4, sizeof(ev));
    }
    for (int i = 0; i < (int)(sizeof(ep->items) / sizeof(ep->items[0])); i++) {
      epoll_item64_t *it = &ep->items[i];
      if (it->used && it->fd == fd) {
        if (op == EPOLL_CTL_MOD) {
          it->events = ev.events;
          it->data = ev.data;
          it->oneshot = (ev.events & EPOLLONESHOT) ? 1 : 0;
          it->disabled = 0;
          return 0;
        }
        if (op == EPOLL_CTL_DEL) {
          memset(it, 0, sizeof(*it));
          return 0;
        }
        return -LINUX_EEXIST;
      }
    }
    if (op == EPOLL_CTL_DEL)
      return -LINUX_ENOENT;
    if (op != EPOLL_CTL_ADD)
      return -LINUX_EINVAL;
    for (int i = 0; i < (int)(sizeof(ep->items) / sizeof(ep->items[0])); i++) {
      epoll_item64_t *it = &ep->items[i];
      if (!it->used) {
        memset(it, 0, sizeof(*it));
        it->used = 1;
        it->fd = fd;
        it->events = ev.events;
        it->data = ev.data;
        it->oneshot = (ev.events & EPOLLONESHOT) ? 1 : 0;
        it->disabled = 0;
        return 0;
      }
    }
    return -LINUX_ENOMEM;
  }

  case LINUX_NR_epoll_wait:
  case LINUX_NR_epoll_pwait: {
    epoll_obj64_t *ep = epoll64_get(task64_current(), (int)a1);
    if (!ep)
      return -LINUX_EBADF;
    linux_epoll_event_t *out = (linux_epoll_event_t *)(uintptr_t)a2;
    int maxevents = (int)a3;
    int timeout = (int)a4;
    if (maxevents <= 0)
      return -LINUX_EINVAL;

    uint64_t start_ms = linux_now_ms();
    while (1) {
      int num = 0;
      for (int i = 0; i < (int)(sizeof(ep->items) / sizeof(ep->items[0])); i++) {
        epoll_item64_t *it = &ep->items[i];
        if (!it->used || it->disabled)
          continue;
        uint32_t revents = epoll64_compute_revents(it);
        if (it->events & EPOLLET) {
          revents &= ~it->last_revents;
        }
        it->last_revents = revents;
        if (revents) {
          out[num].events = revents;
          out[num].data = it->data;
          num++;
          if (it->oneshot)
            it->disabled = 1;
          if (num >= maxevents)
            break;
        }
      }
      if (num > 0)
        return num;
      if (timeout == 0)
        return 0;
      if (timeout > 0 && (int)(linux_now_ms() - start_ms) >= timeout)
        return 0;
      syscall64_dispatch_native(SYS_SLEEP, 1, 0, 0, 0, 0, 0);
    }
  }

  case LINUX_NR_rt_sigaction: {
    int sig = (int)a1;
    if (sig <= 0 || sig > 64 || sig == SIGKILL || sig == SIGSTOP)
      return -LINUX_EINVAL;
    linux_sigaction64_t act;
    auto *t = task64_current();
    if (!t)
      return -LINUX_ESRCH;
    auto *dst = &t->sig_actions[sig - 1];
    if (a3) {
      linux_sigaction64_t old;
      old.handler = (uint64_t)(uintptr_t)dst->handler;
      old.flags = dst->flags;
      old.restorer = 0;
      old.mask = dst->mask;
      linux_copy_to_user((void *)(uintptr_t)a3, &old, sizeof(old));
    }
    if (a2) {
      memcpy(&act, (const void *)(uintptr_t)a2, sizeof(act));
      dst->handler = (void (*)(int))(uintptr_t)act.handler;
      dst->flags = (uint32_t)act.flags;
      dst->mask = act.mask;
    }
    return 0;
  }

  case LINUX_NR_rt_sigprocmask: {
    auto *t = task64_current();
    if (!t)
      return -LINUX_ESRCH;
    uint64_t old = t->sig_mask;
    if (a3)
      linux_copy_to_user((void *)(uintptr_t)a3, &old, sizeof(old));
    if (a2) {
      uint64_t set = 0;
      memcpy(&set, (const void *)(uintptr_t)a2, sizeof(set));
      switch ((int)a1) {
      case SIG_BLOCK:
        t->sig_mask |= set;
        break;
      case SIG_UNBLOCK:
        t->sig_mask &= ~set;
        break;
      case SIG_SETMASK:
        t->sig_mask = set;
        break;
      default:
        return -LINUX_EINVAL;
      }
    }
    return 0;
  }

  case LINUX_NR_rt_sigreturn:
    if (auto *t = task64_current()) {
      if (t->sig_active) {
        syscall64_state_t *st = syscall64_get_state();
        if (st) {
          st->rcx = t->sig_saved_rip;
          t->gs.user_stack = t->sig_saved_rsp;
        }
        t->sig_active = 0;
      }
    }
    return 0;

  case LINUX_NR_kill:
  case LINUX_NR_tkill:
  case LINUX_NR_tgkill: {
    int sig = (int)(num == LINUX_NR_tgkill ? a3 : a2);
    if (sig <= 0 || sig > 64)
      return -LINUX_EINVAL;
    auto *t = task64_current();
    if (!t)
      return -LINUX_ESRCH;
    t->sig_pending |= (1ULL << (sig - 1));
    return 0;
  }

  case LINUX_NR_clone: {
    if (!(a1 & CLONE_VM))
      return -LINUX_ENOSYS;
    uint64_t fs_base = task64_current() ? task64_current()->gs.fs_base : 0;
    if (a1 & CLONE_SETTLS)
      fs_base = a4;
    task64_t *child = task64_clone_current(a2, fs_base);
    if (!child)
      return -LINUX_ENOMEM;
    if (a1 & CLONE_PARENT_SETTID && a3)
      *(uint64_t *)(uintptr_t)a3 = child->id;
    if (a1 & CLONE_CHILD_SETTID && a5)
      *(uint64_t *)(uintptr_t)a5 = child->id;
    if (a1 & CLONE_CHILD_CLEARTID)
      child->clear_tid_addr = a5;
    return (int)child->id;
  }

  case LINUX_NR_gettid:
    return task64_current() ? (int)task64_current()->id : 1;

  case LINUX_NR_set_tid_address:
    if (auto *t = task64_current()) {
      t->clear_tid_addr = a1;
      return (int)t->id;
    }
    return -LINUX_ESRCH;

  case LINUX_NR_arch_prctl:
    return linux_sys_arch_prctl(a1, a2);

  case LINUX_NR_brk: {
    uint64_t res = user64_brk(a1);
    return (int)res;
  }

  case LINUX_NR_mmap: {
    uint64_t addr = user64_mmap(a1, a2, (int)a3, (int)a4);
    if ((int64_t)addr < 0)
      return -LINUX_ENOMEM;
    if (!(a4 & MAP_ANONYMOUS) && (int)a5 >= 0) {
      int r = linux_mmap_load_file(addr, a2, (int)a5, a6);
      if (r < 0) {
        user64_munmap(addr, a2);
        return r;
      }
    }
    return (int)addr;
  }

  case LINUX_NR_munmap:
    if (user64_munmap(a1, a2) < 0)
      return -LINUX_ENOMEM;
    return 0;

  case LINUX_NR_mprotect:
    if (user64_mprotect(a1, a2, (int)a3) < 0)
      return -LINUX_ENOMEM;
    return 0;

  case LINUX_NR_sched_yield:
    return (int)syscall64_dispatch_native(SYS_YIELD, 0, 0, 0, 0, 0, 0);

  case LINUX_NR_nanosleep:
    return linux_sys_nanosleep(a1);

  case LINUX_NR_getpid:
    if (task64_current())
      return (int)task64_current()->id;
    return 1;

  case LINUX_NR_getppid:
    return 0;

  case LINUX_NR_getuid:
  case LINUX_NR_getgid:
    return 0;

  case LINUX_NR_uname:
    return linux_sys_uname(a1);

  case LINUX_NR_sysinfo:
    return linux_sys_sysinfo(a1);

  case LINUX_NR_gettimeofday:
    return linux_sys_gettimeofday(a1);

  case LINUX_NR_time:
    return linux_sys_time(a1);

  case LINUX_NR_clock_gettime:
    return linux_sys_clock_gettime(a1, a2);

  default:
    return -LINUX_ENOSYS;
  }
}

#else

int linux_syscall_dispatch_x86_64(uint64_t num, uint64_t a1, uint64_t a2,
                                  uint64_t a3, uint64_t a4, uint64_t a5,
                                  uint64_t a6) {
  (void)num;
  (void)a1;
  (void)a2;
  (void)a3;
  (void)a4;
  (void)a5;
  (void)a6;
  return -LINUX_ENOSYS;
}

#endif
