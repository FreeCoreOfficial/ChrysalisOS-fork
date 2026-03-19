#include "linux_syscall_i386.h"
#include "linux_abi.h"

#include "../include/chrysalis/syscall_nums.h"
#include "../include/task.h"
#include "../mm/paging.h"
#include "../sched/pcb.h"
#include "../string.h"
#include "../time/timer.h"
#include "../memory/pmm.h"
#include "../mem/kmalloc.h"
#include "../fs/vfs/fs_ops.h"
#include "../user/user.h"
#include "../fs/pipe/pipe.h"

extern "C" int syscall_dispatch_chrys(uint32_t num, uint32_t a1, uint32_t a2,
                                      uint32_t a3, uint32_t a4, uint32_t a5,
                                      uint32_t a6);
extern "C" int syscall_user_range_ok(const void *ptr, uint32_t len);

#define LINUX_EFAULT 14
#define LINUX_EAGAIN 11
#define LINUX_ENOMEM 12
#define LINUX_EINVAL 22
#define LINUX_EEXIST 17
#define LINUX_ENOENT 2
#define LINUX_EBADF 9
#define LINUX_ESRCH 3
#define LINUX_ETIMEDOUT 110
#define LINUX_ENOSYS 38

/* Minimal i386 Linux syscall numbers (subset) */
#define LINUX_NR_exit 1
#define LINUX_NR_read 3
#define LINUX_NR_write 4
#define LINUX_NR_open 5
#define LINUX_NR_close 6
#define LINUX_NR_time 13
#define LINUX_NR_getpid 20
#define LINUX_NR_getuid 24
#define LINUX_NR_getgid 47
#define LINUX_NR_getppid 64
#define LINUX_NR_gettimeofday 78
#define LINUX_NR_sysinfo 116
#define LINUX_NR_uname 122
#define LINUX_NR_exit_group 252
#define LINUX_NR_kill 37
#define LINUX_NR_rt_sigreturn 173
#define LINUX_NR_rt_sigaction 174
#define LINUX_NR_rt_sigprocmask 175
#define LINUX_NR_tkill 238
#define LINUX_NR_futex 240
#define LINUX_NR_pipe 42
#define LINUX_NR_poll 168
#define LINUX_NR_epoll_create 254
#define LINUX_NR_epoll_ctl 255
#define LINUX_NR_epoll_wait 256
#define LINUX_NR_tgkill 270
#define LINUX_NR_epoll_pwait 319
#define LINUX_NR_epoll_create1 329
#define LINUX_NR_pipe2 331

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

#define SIG_BLOCK 0
#define SIG_UNBLOCK 1
#define SIG_SETMASK 2
#define SIGKILL 9
#define SIGTERM 15
#define SIGCHLD 17
#define SIGSTOP 19
#define SIGWINCH 28

typedef struct linux_timespec32 {
  int32_t tv_sec;
  int32_t tv_nsec;
} linux_timespec32_t;

typedef struct linux_sigaction32 {
  uint32_t handler;
  uint32_t flags;
  uint32_t restorer;
  uint64_t mask;
} linux_sigaction32_t;

typedef struct linux_epoll_event {
  uint32_t events;
  uint64_t data;
} linux_epoll_event_t;

typedef struct linux_pollfd {
  int fd;
  short events;
  short revents;
} linux_pollfd_t;

typedef struct futex_waiter {
  uint64_t key;
  task_t *task;
  int in_use;
  int woken;
} futex_waiter_t;

#define FUTEX_MAX_WAITERS 256
static futex_waiter_t g_futex_waiters[FUTEX_MAX_WAITERS];

typedef struct epoll_item {
  int used;
  int fd;
  uint32_t events;
  uint64_t data;
  uint32_t last_revents;
  int oneshot;
  int disabled;
} epoll_item_t;

typedef struct epoll_obj {
  epoll_item_t items[64];
} epoll_obj_t;

static inline uint64_t futex_make_key(task_t *t, uint32_t uaddr) {
  uint64_t cr3 = t ? (uint64_t)t->cr3 : 0;
  return (cr3 << 32) ^ (uint64_t)uaddr;
}

static futex_waiter_t *futex_alloc_waiter(void) {
  for (int i = 0; i < FUTEX_MAX_WAITERS; i++) {
    if (!g_futex_waiters[i].in_use) {
      g_futex_waiters[i].in_use = 1;
      g_futex_waiters[i].woken = 0;
      g_futex_waiters[i].task = nullptr;
      g_futex_waiters[i].key = 0;
      return &g_futex_waiters[i];
    }
  }
  return nullptr;
}

static void futex_free_waiter(futex_waiter_t *w) {
  if (!w)
    return;
  w->in_use = 0;
  w->task = nullptr;
  w->key = 0;
  w->woken = 0;
}

static int futex_wait_impl(uint32_t *uaddr, uint32_t expected,
                           uint64_t timeout_ms) {
  if (!syscall_user_range_ok(uaddr, sizeof(uint32_t)))
    return -LINUX_EFAULT;
  if (__atomic_load_n(uaddr, __ATOMIC_SEQ_CST) != expected)
    return -LINUX_EAGAIN;

  task_t *t = current_task;
  if (!t)
    return -LINUX_EINVAL;

  futex_waiter_t *w = futex_alloc_waiter();
  if (!w)
    return -LINUX_ENOMEM;

  w->task = t;
  w->key = futex_make_key(t, (uint32_t)(uintptr_t)uaddr);

  uint64_t start_ms = timer_uptime_ms();
  while (!w->woken) {
    if (timeout_ms > 0 && (timer_uptime_ms() - start_ms) >= timeout_ms) {
      futex_free_waiter(w);
      return -LINUX_ETIMEDOUT;
    }
    sleep(1);
  }

  futex_free_waiter(w);
  return 0;
}

static int futex_wake_impl(uint32_t *uaddr, int max_to_wake) {
  task_t *t = current_task;
  if (!t)
    return 0;
  uint64_t key = futex_make_key(t, (uint32_t)(uintptr_t)uaddr);
  int woken = 0;
  for (int i = 0; i < FUTEX_MAX_WAITERS && woken < max_to_wake; i++) {
    futex_waiter_t *w = &g_futex_waiters[i];
    if (w->in_use && w->key == key && w->task) {
      w->woken = 1;
      w->task->state = TASK_READY;
      woken++;
    }
  }
  return woken;
}

static int futex_wake_op(uint32_t *uaddr1, int max_to_wake1, uint32_t *uaddr2,
                         int max_to_wake2, uint32_t op_encoded) {
  uint32_t op = (op_encoded >> 28) & 0xF;
  uint32_t op_arg = (op_encoded >> 24) & 0xF;
  uint32_t cmp = (op_encoded >> 12) & 0xFFF;
  uint32_t cmp_arg = op_encoded & 0xFFF;
  uint32_t old_val = 0;

  if (op & FUTEX_OP_OPARG_SHIFT) {
    op_arg = 1u << op_arg;
    op &= ~FUTEX_OP_OPARG_SHIFT;
  }

  switch (op) {
  case FUTEX_OP_SET:
    old_val = __atomic_exchange_n(uaddr2, op_arg, __ATOMIC_SEQ_CST);
    break;
  case FUTEX_OP_ADD:
    old_val = __atomic_fetch_add(uaddr2, op_arg, __ATOMIC_SEQ_CST);
    break;
  case FUTEX_OP_OR:
    old_val = __atomic_fetch_or(uaddr2, op_arg, __ATOMIC_SEQ_CST);
    break;
  case FUTEX_OP_ANDN:
    old_val = __atomic_fetch_nand(uaddr2, op_arg, __ATOMIC_SEQ_CST);
    break;
  case FUTEX_OP_XOR:
    old_val = __atomic_fetch_xor(uaddr2, op_arg, __ATOMIC_SEQ_CST);
    break;
  default:
    return -LINUX_EINVAL;
  }

  int ret1 = futex_wake_impl(uaddr1, max_to_wake1);
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
    ret2 = futex_wake_impl(uaddr2, max_to_wake2);
  return ret1 + ret2;
}

static inline epoll_obj_t *epoll_get(task_t *t, int fd) {
  if (!t)
    return nullptr;
  if (fd < TASK_LINUX_EPOLL_FD_BASE ||
      fd >= TASK_LINUX_EPOLL_FD_BASE + TASK_LINUX_EPOLL_MAX)
    return nullptr;
  return (epoll_obj_t *)t->epoll_table[fd - TASK_LINUX_EPOLL_FD_BASE];
}

static int epoll_alloc_fd(task_t *t) {
  if (!t)
    return -1;
  for (int i = 0; i < TASK_LINUX_EPOLL_MAX; i++) {
    if (!t->epoll_table[i]) {
      epoll_obj_t *ep = (epoll_obj_t *)kmalloc(sizeof(epoll_obj_t));
      if (!ep)
        return -1;
      memset(ep, 0, sizeof(*ep));
      t->epoll_table[i] = ep;
      return TASK_LINUX_EPOLL_FD_BASE + i;
    }
  }
  return -1;
}

static int epoll_close(task_t *t, int fd) {
  if (!t)
    return -LINUX_EBADF;
  if (fd < TASK_LINUX_EPOLL_FD_BASE ||
      fd >= TASK_LINUX_EPOLL_FD_BASE + TASK_LINUX_EPOLL_MAX)
    return -LINUX_EBADF;
  int idx = fd - TASK_LINUX_EPOLL_FD_BASE;
  epoll_obj_t *ep = (epoll_obj_t *)t->epoll_table[idx];
  if (!ep)
    return -LINUX_EBADF;
  kfree(ep);
  t->epoll_table[idx] = nullptr;
  return 0;
}

static uint32_t epoll_compute_revents(task_t *t, epoll_item_t *it) {
  if (!t || !it)
    return 0;
  if (it->fd < 0 || it->fd >= MAX_FILES_PER_PROCESS)
    return 0;
  file_t *f = t->files[it->fd];
  if (!f || !f->node || !f->node->ops)
    return 0;
  uint32_t revents = 0;
  if (f->node->ops->poll) {
    revents = f->node->ops->poll(f->node, it->events);
  } else {
    if ((it->events & EPOLLIN) && f->node->ops->read)
      revents |= EPOLLIN;
    if ((it->events & EPOLLOUT) && f->node->ops->write)
      revents |= EPOLLOUT;
  }
  return revents;
}

static void signal_dispatch(task_t *t) {
  if (!t)
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
    task_exit(sig);
    return;
  }
  linux_sig_action_t *act = &t->sig_actions[sig - 1];
  if (!act->handler || act->handler == (void (*)(int))1) {
    if (sig == SIGCHLD || sig == SIGWINCH)
      return;
    task_exit(sig);
    return;
  }
  act->handler(sig);
}

static int linux_copy_to_user(void *dst, const void *src, uint32_t len) {
  if (!dst || !src || len == 0)
    return -LINUX_EFAULT;
  if (!syscall_user_range_ok(dst, len))
    return -LINUX_EFAULT;
  memcpy(dst, src, len);
  return 0;
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
  strncpy(u->machine, "i386", sizeof(u->machine) - 1);
  strncpy(u->domainname, "(none)", sizeof(u->domainname) - 1);
  u->sysname[sizeof(u->sysname) - 1] = 0;
  u->nodename[sizeof(u->nodename) - 1] = 0;
  u->release[sizeof(u->release) - 1] = 0;
  u->version[sizeof(u->version) - 1] = 0;
  u->machine[sizeof(u->machine) - 1] = 0;
  u->domainname[sizeof(u->domainname) - 1] = 0;
}

static int linux_sys_uname(uint32_t a1) {
  struct linux_utsname u;
  linux_fill_utsname(&u);
  return linux_copy_to_user((void *)(uintptr_t)a1, &u, sizeof(u));
}

static int linux_sys_sysinfo(uint32_t a1) {
  struct linux_sysinfo info;
  memset(&info, 0, sizeof(info));

  info.uptime = (int64_t)timer_uptime_seconds();

  uint32_t total_frames = pmm_total_frames();
  uint32_t used_frames = pmm_used_frames();
  uint32_t free_frames = (total_frames > used_frames) ?
                             (total_frames - used_frames) : 0;

  info.totalram = (uint64_t)total_frames * (uint64_t)PAGE_SIZE;
  info.freeram = (uint64_t)free_frames * (uint64_t)PAGE_SIZE;
  info.sharedram = 0;
  info.bufferram = 0;
  info.totalswap = 0;
  info.freeswap = 0;
  info.procs = 1;
  info.totalhigh = 0;
  info.freehigh = 0;
  info.mem_unit = 1;

  return linux_copy_to_user((void *)(uintptr_t)a1, &info, sizeof(info));
}

static int linux_sys_gettimeofday(uint32_t a1) {
  if (a1 == 0)
    return 0;
  struct linux_timeval64 tv;
  tv.tv_sec = (int64_t)timer_uptime_seconds();
  tv.tv_usec = 0;
  return linux_copy_to_user((void *)(uintptr_t)a1, &tv, sizeof(tv));
}

static int linux_sys_time(uint32_t a1) {
  int64_t now = (int64_t)timer_uptime_seconds();
  if (a1) {
    if (!syscall_user_range_ok((void *)(uintptr_t)a1, sizeof(uint32_t)))
      return -LINUX_EFAULT;
    *(uint32_t *)(uintptr_t)a1 = (uint32_t)now;
  }
  return (int)now;
}

int linux_syscall_dispatch_i386(uint32_t num, uint32_t a1, uint32_t a2,
                                uint32_t a3, uint32_t a4, uint32_t a5,
                                uint32_t a6) {
  (void)a4;
  (void)a5;
  (void)a6;

  pcb_t *cur = pcb_get_current();
  signal_dispatch(current_task);

  switch (num) {
  case LINUX_NR_exit:
  case LINUX_NR_exit_group:
    return syscall_dispatch_chrys(SYS_EXIT, a1, 0, 0, 0, 0, 0);

  case LINUX_NR_read:
    return syscall_dispatch_chrys(SYS_READ, a1, a2, a3, 0, 0, 0);

  case LINUX_NR_write:
    return syscall_dispatch_chrys(SYS_WRITE, a1, a2, a3, 0, 0, 0);

  case LINUX_NR_open:
    return syscall_dispatch_chrys(SYS_OPEN, a1, a2, 0, 0, 0, 0);

  case LINUX_NR_close:
    if (a1 >= TASK_LINUX_EPOLL_FD_BASE)
      return epoll_close(current_task, (int)a1);
    return syscall_dispatch_chrys(SYS_CLOSE, a1, 0, 0, 0, 0, 0);

  case LINUX_NR_pipe:
  case LINUX_NR_pipe2: {
    int *fdp = (int *)(uintptr_t)a1;
    if (!syscall_user_range_ok(fdp, sizeof(int) * 2))
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
      if (!current_task->files[i]) {
        file_t *f = (file_t *)kmalloc(sizeof(file_t));
        if (!f)
          return -LINUX_ENOMEM;
        f->node = rnode;
        f->offset = 0;
        f->flags = 0;
        current_task->files[i] = f;
        rfd = i;
        break;
      }
    }
    for (int i = 0; i < MAX_FILES_PER_PROCESS; i++) {
      if (!current_task->files[i]) {
        file_t *f = (file_t *)kmalloc(sizeof(file_t));
        if (!f)
          return -LINUX_ENOMEM;
        f->node = wnode;
        f->offset = 0;
        f->flags = 0;
        current_task->files[i] = f;
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
    if (nfds > 0 &&
        !syscall_user_range_ok(fds, sizeof(linux_pollfd_t) * (uint32_t)nfds))
      return -LINUX_EFAULT;

    uint64_t start = timer_uptime_ms();
    while (1) {
      int ready = 0;
      for (int i = 0; i < nfds; i++) {
        linux_pollfd_t *pfd = &fds[i];
        pfd->revents = 0;
        if (pfd->fd < 0 || pfd->fd >= MAX_FILES_PER_PROCESS)
          continue;
        file_t *f = current_task->files[pfd->fd];
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
        pfd->revents = (short)re;
        if (re)
          ready++;
      }
      if (ready || timeout == 0)
        return ready;
      if (timeout > 0 && (int)(timer_uptime_ms() - start) >= timeout)
        return 0;
      sleep(1);
    }
  }

  case LINUX_NR_futex: {
    int op = (int)a2;
    uint32_t cmd = (uint32_t)(op & FUTEX_CMD_MASK);
    uint64_t timeout_ms = 0;
    if (cmd == FUTEX_WAIT || cmd == FUTEX_WAIT_BITSET) {
      if (a4) {
        linux_timespec32_t ts;
        if (!syscall_user_range_ok((void *)(uintptr_t)a4, sizeof(ts)))
          return -LINUX_EFAULT;
        memcpy(&ts, (const void *)(uintptr_t)a4, sizeof(ts));
        if (ts.tv_sec < 0 || ts.tv_nsec < 0)
          return -LINUX_EINVAL;
        timeout_ms = (uint64_t)ts.tv_sec * 1000ULL +
                     (uint64_t)ts.tv_nsec / 1000000ULL;
      }
    }
    switch (cmd) {
    case FUTEX_WAIT:
      return futex_wait_impl((uint32_t *)(uintptr_t)a1, (uint32_t)a3,
                             timeout_ms);
    case FUTEX_WAKE:
      return futex_wake_impl((uint32_t *)(uintptr_t)a1, (int)a3);
    case FUTEX_WAIT_BITSET:
      return futex_wait_impl((uint32_t *)(uintptr_t)a1, (uint32_t)a3,
                             timeout_ms);
    case FUTEX_WAKE_BITSET:
      return futex_wake_impl((uint32_t *)(uintptr_t)a1, (int)a3);
    case FUTEX_WAKE_OP:
      return futex_wake_op((uint32_t *)(uintptr_t)a1, (int)a3,
                           (uint32_t *)(uintptr_t)a5, (int)a4, (uint32_t)a6);
    default:
      return -LINUX_ENOSYS;
    }
  }

  case LINUX_NR_epoll_create:
    if ((int)a1 <= 0)
      return -LINUX_EINVAL;
    return epoll_alloc_fd(current_task);

  case LINUX_NR_epoll_create1:
    return epoll_alloc_fd(current_task);

  case LINUX_NR_epoll_ctl: {
    epoll_obj_t *ep = epoll_get(current_task, (int)a1);
    if (!ep)
      return -LINUX_EBADF;
    int op = (int)a2;
    int fd = (int)a3;
    linux_epoll_event_t ev;
    if (op != EPOLL_CTL_DEL) {
      if (!syscall_user_range_ok((void *)(uintptr_t)a4, sizeof(ev)))
        return -LINUX_EFAULT;
      memcpy(&ev, (const void *)(uintptr_t)a4, sizeof(ev));
    }
    for (int i = 0; i < (int)(sizeof(ep->items) / sizeof(ep->items[0])); i++) {
      epoll_item_t *it = &ep->items[i];
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
      epoll_item_t *it = &ep->items[i];
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
    epoll_obj_t *ep = epoll_get(current_task, (int)a1);
    if (!ep)
      return -LINUX_EBADF;
    linux_epoll_event_t *out = (linux_epoll_event_t *)(uintptr_t)a2;
    int maxevents = (int)a3;
    int timeout = (int)a4;
    if (maxevents <= 0)
      return -LINUX_EINVAL;
    if (!syscall_user_range_ok(out, sizeof(linux_epoll_event_t) * (uint32_t)maxevents))
      return -LINUX_EFAULT;

    uint64_t start_ms = timer_uptime_ms();
    while (1) {
      int num = 0;
      for (int i = 0; i < (int)(sizeof(ep->items) / sizeof(ep->items[0])); i++) {
        epoll_item_t *it = &ep->items[i];
        if (!it->used || it->disabled)
          continue;
        uint32_t revents = epoll_compute_revents(current_task, it);
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
      if (timeout > 0 && (int)(timer_uptime_ms() - start_ms) >= timeout)
        return 0;
      sleep(1);
    }
  }

  case LINUX_NR_rt_sigaction: {
    int sig = (int)a1;
    if (sig <= 0 || sig > 64 || sig == SIGKILL || sig == SIGSTOP)
      return -LINUX_EINVAL;
    linux_sigaction32_t act;
    linux_sig_action_t *dst = &current_task->sig_actions[sig - 1];
    if (a3) {
      linux_sigaction32_t old;
      old.handler = (uint32_t)(uintptr_t)dst->handler;
      old.flags = dst->flags;
      old.restorer = 0;
      old.mask = dst->mask;
      if (linux_copy_to_user((void *)(uintptr_t)a3, &old, sizeof(old)) < 0)
        return -LINUX_EFAULT;
    }
    if (a2) {
      if (!syscall_user_range_ok((void *)(uintptr_t)a2, sizeof(act)))
        return -LINUX_EFAULT;
      memcpy(&act, (const void *)(uintptr_t)a2, sizeof(act));
      dst->handler = (void (*)(int))(uintptr_t)act.handler;
      dst->flags = act.flags;
      dst->mask = act.mask;
    }
    return 0;
  }

  case LINUX_NR_rt_sigprocmask: {
    uint64_t old = current_task->sig_mask;
    if (a3) {
      if (linux_copy_to_user((void *)(uintptr_t)a3, &old, sizeof(old)) < 0)
        return -LINUX_EFAULT;
    }
    if (a2) {
      uint64_t set = 0;
      if (!syscall_user_range_ok((void *)(uintptr_t)a2, sizeof(set)))
        return -LINUX_EFAULT;
      memcpy(&set, (const void *)(uintptr_t)a2, sizeof(set));
      switch ((int)a1) {
      case SIG_BLOCK:
        current_task->sig_mask |= set;
        break;
      case SIG_UNBLOCK:
        current_task->sig_mask &= ~set;
        break;
      case SIG_SETMASK:
        current_task->sig_mask = set;
        break;
      default:
        return -LINUX_EINVAL;
      }
    }
    return 0;
  }

  case LINUX_NR_rt_sigreturn:
    return 0;

  case LINUX_NR_kill:
  case LINUX_NR_tkill:
  case LINUX_NR_tgkill: {
    int sig = (int)(num == LINUX_NR_tgkill ? a3 : a2);
    if (sig <= 0 || sig > 64)
      return -LINUX_EINVAL;
    if (!current_task)
      return -LINUX_ESRCH;
    current_task->sig_pending |= (1ULL << (sig - 1));
    return 0;
  }

  case LINUX_NR_getpid:
    return cur ? cur->pid : 1;

  case LINUX_NR_getppid:
    return 0;

  case LINUX_NR_getuid: {
    user_t *u = user_get_current();
    return u ? (int)u->uid : 0;
  }

  case LINUX_NR_getgid: {
    user_t *u = user_get_current();
    return u ? (int)u->gid : 0;
  }

  case LINUX_NR_uname:
    return linux_sys_uname(a1);

  case LINUX_NR_sysinfo:
    return linux_sys_sysinfo(a1);

  case LINUX_NR_gettimeofday:
    return linux_sys_gettimeofday(a1);

  case LINUX_NR_time:
    return linux_sys_time(a1);

  default:
    return -LINUX_ENOSYS;
  }
}
