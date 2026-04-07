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
#include "../fs/vfs/vnode.h"
#include "../fs/vfs/fs_ops.h"
#include "../fs/ramfs/ramfs.h"
#include "../drivers/serial.h"
#include "../hardware/msr.h"
#include "../video/gpu.h"
#include "../video/kms.h"
#include "../fs/devfs/devfs.h"
#include "../fs/pipe/pipe.h"

#define MSR_FS_BASE 0xC0000100u
#define MSR_GS_BASE 0xC0000101u
#define MSR_KERNEL_GS_BASE 0xC0000102u
extern "C" void syscall64_exit(void);
#include "../arch/x86_64/paging64.h"
#include "../proc/exec64.h"
#include <stddef.h>

#ifndef OFFSETOF
#define OFFSETOF(type, member) ((size_t)__builtin_offsetof(type, member))
#endif




#define LINUX_EFAULT 14
#define LINUX_EAGAIN 11
#define LINUX_ENOMEM 12
#define LINUX_EINVAL 22
#define LINUX_ENOSYS 38
#define LINUX_EEXIST 17
#define LINUX_ENOENT 2
#define LINUX_EBADF 9
#define LINUX_ESRCH 3
#define LINUX_ECHILD 10
#define LINUX_EINTR 4
#define LINUX_ENOTDIR 20
#define LINUX_ETIMEDOUT 110
#define LINUX_EADDRINUSE 98
#define LINUX_ECONNREFUSED 111
#define LINUX_EAFNOSUPPORT 97
#define LINUX_ENOPROTOOPT 92
#define LINUX_EPROTONOSUPPORT 93
#define LINUX_EPIPE 32
#define LINUX_AT_FDCWD -100


#if defined(__x86_64__)


/* Minimal x86_64 Linux syscall numbers (subset) */
#define LINUX_NR_read 0
#define LINUX_NR_write 1
#define LINUX_NR_open 2
#define LINUX_NR_close 3
#define LINUX_NR_execve 59
#define LINUX_NR_stat 4
#define LINUX_NR_fstat 5
#define LINUX_NR_lstat 6
#define LINUX_NR_lseek 8
#define LINUX_NR_mmap 9
#define LINUX_NR_ioctl 16
#define LINUX_NR_mprotect 10
#define LINUX_NR_munmap 11
#define LINUX_NR_brk 12
#define LINUX_NR_poll 7
#define LINUX_NR_pipe 22
#define LINUX_NR_mremap 25
#define LINUX_NR_sched_yield 24
#define LINUX_NR_nanosleep 35
#define LINUX_NR_alarm 37
#define LINUX_NR_socket 41
#define LINUX_NR_connect 42
#define LINUX_NR_accept 43
#define LINUX_NR_sendto 44
#define LINUX_NR_recvfrom 45
#define LINUX_NR_sendmsg 46
#define LINUX_NR_recvmsg 47
#define LINUX_NR_shutdown 48
#define LINUX_NR_bind 49
#define LINUX_NR_listen 50
#define LINUX_NR_getsockname 51
#define LINUX_NR_getpeername 52
#define LINUX_NR_socketpair 53
#define LINUX_NR_setsockopt 54
#define LINUX_NR_getsockopt 55
#define LINUX_NR_chdir 80
#define LINUX_NR_fchdir 81
#define LINUX_NR_link 86
#define LINUX_NR_unlink 87
#define LINUX_NR_fchmod 91
#define LINUX_NR_getpid 39
#define LINUX_NR_uname 63
#define LINUX_NR_gettimeofday 96
#define LINUX_NR_fcntl 72
#define LINUX_NR_sysinfo 99
#define LINUX_NR_getuid 102
#define LINUX_NR_getgid 104
#define LINUX_NR_geteuid 107
#define LINUX_NR_getegid 108
#define LINUX_NR_setuid 105
#define LINUX_NR_setgid 106
#define LINUX_NR_getppid 110
#define LINUX_NR_getpgrp 111
#define LINUX_NR_getresgid 120
#define LINUX_NR_rt_sigsuspend 130
#define LINUX_NR_statfs 137
#define LINUX_NR_setpriority 141
#define LINUX_NR_prctl 157
#define LINUX_NR_time 201
#define LINUX_NR_clock_gettime 228
#define LINUX_NR_clock_nanosleep 230
#define LINUX_NR_exit 60
#define LINUX_NR_exit_group 231
#define LINUX_NR_openat 257
#define LINUX_NR_access 21
#define LINUX_NR_newfstatat 262
#define LINUX_NR_writev 20
#define LINUX_NR_pread64 17
#define LINUX_NR_getdents64 217
#define LINUX_NR_set_robust_list 273
#define LINUX_NR_rseq 334
#define LINUX_NR_fork 57
#define LINUX_NR_vfork 58
#define LINUX_NR_wait4 61
#define LINUX_NR_waitid 247

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
#define LINUX_NR_dup3 292
#define LINUX_NR_tgkill 234
#define LINUX_NR_clone 56
#define LINUX_NR_arch_prctl 158
#define LINUX_NR_gettid 186
#define LINUX_NR_set_tid_address 218
#define LINUX_NR_readlink 89
#define LINUX_NR_umask 95
#define LINUX_NR_dup 32
#define LINUX_NR_dup2 33
#define LINUX_NR_getresuid 118
#define LINUX_NR_clock_getres 229


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

#define ARCH_SET_GS 0x1001
#define ARCH_SET_FS 0x1002
#define ARCH_GET_FS 0x1003
#define ARCH_GET_GS 0x1004

#ifndef MSR_FS_BASE
#define MSR_FS_BASE 0xC0000100u
#endif
#ifndef MSR_GS_BASE
#define MSR_GS_BASE 0xC0000101u
#endif

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

#define LINUX_AF_UNIX 1
#define LINUX_SOL_SOCKET 1
#define LINUX_SOCK_STREAM 1
#define LINUX_SOCK_TYPE_MASK 0xF
#define LINUX_SOCK_NONBLOCK 0x800
#define LINUX_SOCK_CLOEXEC 0x80000
#define LINUX_O_NONBLOCK 0x800
#define LINUX_O_CLOEXEC 0x80000
#define LINUX_O_ASYNC 0x2000
#define LINUX_F_DUPFD 0
#define LINUX_F_GETFD 1
#define LINUX_F_SETFD 2
#define LINUX_F_GETFL 3
#define LINUX_F_SETFL 4
#define LINUX_F_SETOWN 8
#define LINUX_F_GETOWN 9
#define LINUX_F_DUPFD_CLOEXEC 1030
#define LINUX_FD_CLOEXEC 1
#define LINUX_SO_TYPE 3
#define LINUX_SO_ERROR 4

#define PROT_READ 0x1
#define PROT_WRITE 0x2
#define PROT_EXEC 0x4
#define MAP_FIXED 0x10
#define MAP_ANONYMOUS 0x20
#define MREMAP_MAYMOVE 0x1

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

typedef struct linux_sockaddr_un {
  uint16_t sun_family;
  char sun_path[108];
} linux_sockaddr_un_t;

typedef struct linux_socket64 {
  int in_use;
  int domain;
  int type;
  int protocol;
  int state; /* 0=init,1=bound,2=listen,3=connected,4=closed */
  int peer;
  int backlog;
  int accept_q[8];
  int aq_head;
  int aq_tail;
  int aq_len;
  char path[108];
  uint8_t buf[4096];
  uint32_t rpos;
  uint32_t wpos;
  uint32_t used;
  vnode_t vnode;
} linux_socket64_t;

static linux_socket64_t g_sock64[64];
static fs_ops_t g_sock_ops;
static int g_sock_ops_init = 0;

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

typedef struct linux_statfs64 {
  uint64_t f_type;
  uint64_t f_bsize;
  uint64_t f_blocks;
  uint64_t f_bfree;
  uint64_t f_bavail;
  uint64_t f_files;
  uint64_t f_ffree;
  struct {
    int32_t val[2];
  } f_fsid;
  uint64_t f_namelen;
  uint64_t f_frsize;
  uint64_t f_flags;
  uint64_t f_spare[4];
} linux_statfs64_t;

typedef struct linux_dirent64 {
  uint64_t d_ino;
  int64_t d_off;
  uint16_t d_reclen;
  uint8_t d_type;
  char d_name[];
} linux_dirent64_t;

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

static int linux_copy_user_cstr(const char *src, char *dst, uint32_t dst_size) {
  if (!src || !dst || dst_size == 0)
    return -LINUX_EFAULT;
  uint32_t i = 0;
  for (; i + 1 < dst_size; ++i) {
    char c = src[i];
    dst[i] = c;
    if (c == '\0')
      return (int)i;
  }
  dst[dst_size - 1] = '\0';
  return (int)(dst_size - 1);
}

static int linux_copy_resolved_path(const char *src, char *dst,
                                    uint32_t dst_size) {
  char tmp[TASK64_CWD_MAX];
  int rc = linux_copy_user_cstr(src, tmp, sizeof(tmp));
  if (rc < 0)
    return rc;
  if (syscall64_resolve_path(tmp, dst, dst_size) < 0)
    return -LINUX_ENOENT;
  return 0;
}

static int linux_vnode_build_path(vnode_t *node, char *out, uint32_t out_size) {
  if (!node || !out || out_size == 0)
    return -LINUX_EFAULT;
  if (!node->parent) {
    strncpy(out, "/", out_size);
    out[out_size - 1] = 0;
    return 0;
  }

  const char *parts[64];
  int count = 0;
  vnode_t *cur = node;
  while (cur && cur->parent && count < (int)(sizeof(parts) / sizeof(parts[0]))) {
    parts[count++] = cur->name ? cur->name : "";
    cur = cur->parent;
  }

  out[0] = '/';
  out[1] = 0;
  for (int i = count - 1; i >= 0; --i) {
    if (strlen(out) > 1 && strlen(out) + 1 < out_size)
      strcat(out, "/");
    if (strlen(out) + strlen(parts[i]) < out_size)
      strcat(out, parts[i]);
  }
  return 0;
}

static int linux_fill_statfs(uint64_t user_ptr) {
  if (!user_ptr)
    return -LINUX_EFAULT;
  linux_statfs64_t st;
  memset(&st, 0, sizeof(st));
  st.f_type = 0x858458f6ULL;
  st.f_bsize = 4096;
  st.f_frsize = 4096;
  st.f_namelen = 255;
  memcpy((void *)(uintptr_t)user_ptr, &st, sizeof(st));
  return 0;
}

static uint8_t linux_dtype_from_vnode(vnode_t *node) {
  if (!node)
    return 0;
  switch (node->type) {
  case VNODE_DIR:
    return 4; /* DT_DIR */
  case VNODE_FILE:
    return 8; /* DT_REG */
  case VNODE_LNK:
    return 10; /* DT_LNK */
  default:
    return 0; /* DT_UNKNOWN */
  }
}

static int linux_sys_getdents64(int fd, uint64_t dirp, uint32_t count) {
  if (!dirp || count == 0)
    return -LINUX_EINVAL;
  if (fd < 0 || fd >= MAX_FILES_PER_PROCESS)
    return -LINUX_EBADF;
  file_t *f = syscall64_get_file(fd);
  if (!f || !f->node)
    return -LINUX_EBADF;
  vnode_t *dir = f->node;
  if (dir->type != VNODE_DIR || !dir->ops || !dir->ops->readdir)
    return -LINUX_ENOTDIR;

  uint8_t *out = (uint8_t *)(uintptr_t)dirp;
  uint32_t written = 0;
  uint32_t index = f->offset;
  while (1) {
    vnode_t *child = nullptr;
    int rc = dir->ops->readdir(dir, index, &child);
    if (rc <= 0 || !child)
      break;

    const char *name = child->name ? child->name : "";
    uint32_t namelen = (uint32_t)strlen(name);
    uint16_t reclen = (uint16_t)((OFFSETOF(linux_dirent64_t, d_name) + namelen + 1u + 7u) & ~7u);
    if (reclen > count)
      return -LINUX_EINVAL;
    if (written + reclen > count)
      break;

    linux_dirent64_t *ent = (linux_dirent64_t *)(out + written);
    memset(ent, 0, reclen);
    ent->d_ino = (uint64_t)(uintptr_t)child;
    ent->d_off = (int64_t)(index + 1);
    ent->d_reclen = reclen;
    ent->d_type = linux_dtype_from_vnode(child);
    memcpy(ent->d_name, name, namelen);
    ent->d_name[namelen] = 0;

    written += reclen;
    index++;
  }

  f->offset = index;
  return (int)written;
}

static uint16_t g_linux_umask = 0022;

static int linux_sys_prctl(uint64_t option, uint64_t arg2, uint64_t arg3,
                           uint64_t arg4, uint64_t arg5) {
  (void)arg3;
  (void)arg4;
  (void)arg5;
  task64_t *t = task64_current();
  switch ((int)option) {
  case 1:  /* PR_SET_PDEATHSIG */
  case 4:  /* PR_SET_DUMPABLE */
  case 15: /* PR_SET_NAME */
  case 38: /* PR_SET_NO_NEW_PRIVS */
    if ((int)option == 15 && t && arg2) {
      const char *name = (const char *)(uintptr_t)arg2;
      size_t n = strlen(name);
      if (n > sizeof(t->name) - 1)
        n = sizeof(t->name) - 1;
      memcpy(t->name, name, n);
      t->name[n] = 0;
    }
    return 0;
  case 2: /* PR_GET_PDEATHSIG */
    if (!arg2)
      return -LINUX_EFAULT;
    *(int *)(uintptr_t)arg2 = 0;
    return 0;
  case 3: /* PR_GET_DUMPABLE */
    return 1;
  case 16: /* PR_GET_NAME */
    if (!arg2)
      return -LINUX_EFAULT;
    memset((void *)(uintptr_t)arg2, 0, 16);
    if (t) {
      size_t n = strlen(t->name);
      if (n > 15)
        n = 15;
      memcpy((void *)(uintptr_t)arg2, t->name, n);
    }
    return 0;
  case 23: /* PR_CAPBSET_READ */
    return 0;
  case 39: /* PR_GET_NO_NEW_PRIVS */
    return 1;
  default:
    return -LINUX_EINVAL;
  }
}

static linux_socket64_t *sock64_from_vnode(vnode_t *n) {
  if (!n)
    return nullptr;
  return (linux_socket64_t *)n->internal;
}

static uint32_t sock64_poll(struct vnode *n, uint32_t events) {
  linux_socket64_t *s = sock64_from_vnode(n);
  if (!s || s->state == 4)
    return 0u;
  uint32_t re = 0;
  if ((events & POLLIN) && s->used > 0)
    re |= POLLIN;
  if ((events & POLLOUT) && s->peer >= 0)
    re |= POLLOUT;
  return re;
}

static int sock64_read(struct vnode *n, uint32_t off, uint8_t *buf,
                       uint32_t size) {
  (void)off;
  if (!n || !buf || size == 0)
    return 0;
  linux_socket64_t *s = sock64_from_vnode(n);
  if (!s || s->state == 4)
    return 0;
  while (s->used == 0 && s->state != 4) {
    task64_yield();
  }
  if (s->used == 0)
    return 0;
  uint32_t to_read = size < s->used ? size : s->used;
  for (uint32_t i = 0; i < to_read; i++) {
    buf[i] = s->buf[s->rpos];
    s->rpos = (s->rpos + 1) % (uint32_t)sizeof(s->buf);
  }
  s->used -= to_read;
  return (int)to_read;
}

static int sock64_write(struct vnode *n, uint32_t off, const uint8_t *buf,
                        uint32_t size) {
  (void)off;
  if (!n || !buf || size == 0)
    return 0;
  linux_socket64_t *s = sock64_from_vnode(n);
  if (!s || s->state == 4 || s->peer < 0)
    return -LINUX_EPIPE;
  linux_socket64_t *peer = &g_sock64[s->peer];
  if (!peer->in_use || peer->state == 4)
    return -LINUX_EPIPE;
  uint32_t space = (uint32_t)sizeof(peer->buf) - peer->used;
  uint32_t to_write = size < space ? size : space;
  for (uint32_t i = 0; i < to_write; i++) {
    peer->buf[peer->wpos] = buf[i];
    peer->wpos = (peer->wpos + 1) % (uint32_t)sizeof(peer->buf);
  }
  peer->used += to_write;
  return (int)to_write;
}

static int sock64_close(struct vnode *n) {
  linux_socket64_t *s = sock64_from_vnode(n);
  if (!s)
    return 0;
  s->state = 4;
  if (s->peer >= 0 &&
      s->peer < (int)(sizeof(g_sock64) / sizeof(g_sock64[0]))) {
    linux_socket64_t *peer = &g_sock64[s->peer];
    peer->peer = -1;
  }
  s->peer = -1;
  s->aq_head = s->aq_tail = s->aq_len = 0;
  s->used = 0;
  s->rpos = s->wpos = 0;
  s->path[0] = 0;
  s->in_use = 0;
  s->vnode.internal = nullptr;
  return 0;
}

static void sock64_init_ops(void) {
  if (g_sock_ops_init)
    return;
  memset(&g_sock_ops, 0, sizeof(g_sock_ops));
  g_sock_ops.read = sock64_read;
  g_sock_ops.write = sock64_write;
  g_sock_ops.close = sock64_close;
  g_sock_ops.poll = sock64_poll;
  g_sock_ops_init = 1;
}

static int sock64_alloc(void) {
  for (int i = 0; i < (int)(sizeof(g_sock64) / sizeof(g_sock64[0])); i++) {
    if (!g_sock64[i].in_use) {
      memset(&g_sock64[i], 0, sizeof(g_sock64[i]));
      g_sock64[i].in_use = 1;
      g_sock64[i].peer = -1;
      g_sock64[i].state = 0;
      g_sock64[i].vnode.ops = &g_sock_ops;
      g_sock64[i].vnode.type = VNODE_DEV;
      g_sock64[i].vnode.internal = &g_sock64[i];
      return i;
    }
  }
  return -1;
}

static linux_socket64_t *sock64_get_by_path(const char *path) {
  if (!path || !*path)
    return nullptr;
  for (int i = 0; i < (int)(sizeof(g_sock64) / sizeof(g_sock64[0])); i++) {
    if (!g_sock64[i].in_use)
      continue;
    if (g_sock64[i].state != 2)
      continue;
    if (strcmp(g_sock64[i].path, path) == 0)
      return &g_sock64[i];
  }
  return nullptr;
}

static int sock64_enqueue(linux_socket64_t *listener, int idx) {
  if (!listener || listener->state != 2)
    return -1;
  if (listener->aq_len >=
      (int)(sizeof(listener->accept_q) / sizeof(listener->accept_q[0])))
    return -1;
  listener->accept_q[listener->aq_tail] = idx;
  listener->aq_tail =
      (listener->aq_tail + 1) %
      (int)(sizeof(listener->accept_q) / sizeof(listener->accept_q[0]));
  listener->aq_len++;
  return 0;
}

static int sock64_dequeue(linux_socket64_t *listener) {
  if (!listener || listener->aq_len == 0)
    return -1;
  int idx = listener->accept_q[listener->aq_head];
  listener->aq_head =
      (listener->aq_head + 1) %
      (int)(sizeof(listener->accept_q) / sizeof(listener->accept_q[0]));
  listener->aq_len--;
  return idx;
}

static int linux_fd_alloc(vnode_t *node, int flags) {
  for (int i = 3; i < MAX_FILES_PER_PROCESS; i++) {
    if (!syscall64_get_file(i)) {
      file_t *f = (file_t *)kmalloc(sizeof(file_t));
      if (!f)
        return -LINUX_ENOMEM;
      memset(f, 0, sizeof(*f));
      f->node = node;
      f->offset = 0;
      f->flags = flags & (LINUX_O_NONBLOCK | LINUX_O_ASYNC);
      f->refcount = 1;
      syscall64_set_fd_flags(i, (flags & LINUX_O_CLOEXEC) ? LINUX_FD_CLOEXEC : 0);
      syscall64_set_file(i, f);
      return i;
    }
  }
  return -LINUX_ENOMEM;
}

static int linux_mmap_load_file(uint64_t addr, uint64_t len, int fd,
                                uint64_t file_off) {
  if (fd < 0 || fd >= MAX_FILES_PER_PROCESS)
    return -LINUX_EBADF;
  file_t *f = syscall64_get_file(fd);
  if (!f || !f->node || !f->node->ops || !f->node->ops->read)
    return -LINUX_EBADF;

  serial_write_string("[LINUX] mmap_load: addr=");
  serial_printf("0x%x", addr);
  serial_write_string(" len=");
  serial_printf("%u", len);
  serial_write_string(" off=");
  serial_printf("%u", file_off);
  serial_write_string("\r\n");

  uint64_t remaining = len;
  uint64_t dst = addr;
  uint64_t off_curr = file_off;
  uint8_t tmp[256];
  while (remaining > 0) {
    uint32_t chunk =
        remaining > sizeof(tmp) ? (uint32_t)sizeof(tmp) : (uint32_t)remaining;
    int r = f->node->ops->read(f->node, (uint32_t)off_curr, tmp, chunk);
    if (r <= 0)
      break;
    memcpy((void *)(uintptr_t)dst, tmp, (uint32_t)r);
    dst += (uint32_t)r;
    off_curr += (uint32_t)r;
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

static int futex64_wait_impl(syscall64_state_t *state, uint64_t uaddr, uint32_t expected,
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
    syscall64_dispatch_native(state, SYS_SLEEP, 1, 0, 0, 0, 0, 0);
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

static void task64_force_exit(task64_t *t, int code) {
  if (!t)
    return;
  t->exit_code = code;
  t->state = TASK64_ZOMBIE;
  if (t->parent_id) {
    task64_t *parent = task64_find_by_id(t->parent_id);
    if (parent)
      parent->sig_pending |= (1ULL << (SIGCHLD - 1));
  }
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

static int linux_sys_uname(syscall64_state_t *state, uint64_t a1) {
  (void)state;
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

static int linux_sys_gettimeofday(syscall64_state_t *state, uint64_t a1) {
  (void)state;
  if (a1 == 0)
    return 0;
  struct datetime t;
  time_get_utc(&t);
  struct linux_timeval64 tv;
  tv.tv_sec = (int64_t)linux_epoch_from_datetime(&t);
  tv.tv_usec = 0;
  return linux_copy_to_user((void *)(uintptr_t)a1, &tv, sizeof(tv));
}

static int linux_sys_time(syscall64_state_t *state, uint64_t a1) {
  (void)state;
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
  st->st_dev = 1;

  st->st_ino = (uint64_t)(uintptr_t)node;
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

static int linux_sys_stat64(syscall64_state_t *state, const char *path, linux_stat64_t *out) {
  (void)state;
  if (!path || !out)
    return -LINUX_EFAULT;
  char kpath[TASK64_CWD_MAX];
  int rc = linux_copy_resolved_path(path, kpath, sizeof(kpath));
  if (rc < 0)
    return rc;
  vnode_t *node = vfs_resolve(kpath);
  if (!node)
    return -LINUX_ENOENT;
  linux_stat64_t st;
  linux_fill_stat64(node, &st);
  return linux_copy_to_user((void *)(uintptr_t)out, &st, sizeof(st));
}


static int linux_sys_readlink(syscall64_state_t *state, const char *path, char *buf, uint32_t bufsize) {
  (void)state;
  if (!path || !buf || bufsize == 0)
    return -LINUX_EFAULT;
  char kpath[256];
  int rc = linux_copy_resolved_path(path, kpath, sizeof(kpath));
  if (rc < 0)
    return rc;
  if (strcmp(kpath, "/sys/class/graphics/fb0/device/subsystem") == 0) {
    static const char k_fb0_subsystem[] = "/sys/bus/platform";
    uint32_t len = (uint32_t)strlen(k_fb0_subsystem);
    if (len >= bufsize)
      len = bufsize - 1;
    if (linux_copy_to_user(buf, k_fb0_subsystem, len) < 0)
      return -LINUX_EFAULT;
    serial_write_string("[LINUX] readlink synthetic fb0 subsystem\n");
    return (int)len;
  }
  vnode_t *node = vfs_resolve(kpath);
  if (!node)
    return -LINUX_ENOENT;
  if (node->type != VNODE_LNK || !node->ops || !node->ops->readlink) {
    /* If it's not a link, Linux readlink returns EINVAL */
    return -LINUX_EINVAL;
  }
  return node->ops->readlink(node, buf, bufsize);
}

static int linux_sys_chdir(const char *path) {
  task64_t *t = task64_current();
  if (!path)
    return -LINUX_EFAULT;
  if (!t)
    return -LINUX_ESRCH;
  char kpath[TASK64_CWD_MAX];
  int rc = linux_copy_resolved_path(path, kpath, sizeof(kpath));
  if (rc < 0)
    return rc;
  vnode_t *node = vfs_resolve(kpath);
  if (!node)
    return -LINUX_ENOENT;
  if (node->type != VNODE_DIR)
    return -LINUX_ENOTDIR;
  strncpy(t->cwd, kpath, sizeof(t->cwd) - 1);
  t->cwd[sizeof(t->cwd) - 1] = 0;
  return 0;
}

static int linux_sys_fchdir(int fd) {
  task64_t *t = task64_current();
  if (!t)
    return -LINUX_ESRCH;
  file_t *f = syscall64_get_file(fd);
  if (!f || !f->node)
    return -LINUX_EBADF;
  if (f->node->type != VNODE_DIR)
    return -LINUX_ENOTDIR;
  char path[TASK64_CWD_MAX];
  int rc = linux_vnode_build_path(f->node, path, sizeof(path));
  if (rc < 0)
    return rc;
  strncpy(t->cwd, path, sizeof(t->cwd) - 1);
  t->cwd[sizeof(t->cwd) - 1] = 0;
  return 0;
}


static int linux_sys_fstat64(syscall64_state_t *state, int fd, linux_stat64_t *out) {
  (void)state;
  if (!out)
    return -LINUX_EFAULT;
  file_t *f = syscall64_get_file(fd);
  if (!f || !f->node)
    return -LINUX_EBADF;
  linux_stat64_t st;
  linux_fill_stat64(f->node, &st);
  return linux_copy_to_user(out, &st, sizeof(st));
}

static int linux_sys_clock_gettime(syscall64_state_t *state, uint64_t clk_id, uint64_t out_ptr) {
  (void)state;
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

static int linux_sys_sysinfo(syscall64_state_t *state, uint64_t a1) {
  (void)state;
  if (!a1)
    return -LINUX_EFAULT;
  struct linux_sysinfo info;
  memset(&info, 0, sizeof(info));
  info.uptime = 0;
  info.mem_unit = 1;
  return linux_copy_to_user((void *)(uintptr_t)a1, &info, sizeof(info));
}

static int linux_sys_nanosleep(syscall64_state_t *state, uint64_t a1) {
  if (!a1)
    return -LINUX_EFAULT;
  struct linux_timespec64 req;
  memcpy(&req, (const void *)(uintptr_t)a1, sizeof(req));
  if (req.tv_sec < 0 || req.tv_nsec < 0)
    return -LINUX_EINVAL;
  uint64_t ms = (uint64_t)req.tv_sec * 1000ULL +
                (uint64_t)req.tv_nsec / 1000000ULL;
  syscall64_dispatch_native(state, SYS_SLEEP, ms, 0, 0, 0, 0, 0);
  return 0;
}

static int linux_sys_clock_nanosleep(syscall64_state_t *state, uint64_t clk_id,
                                     uint64_t flags, uint64_t req_ptr,
                                     uint64_t) {
  (void)clk_id;
  (void)flags;
  if (!req_ptr)
    return -LINUX_EFAULT;
  struct linux_timespec64 req;
  memcpy(&req, (const void *)(uintptr_t)req_ptr, sizeof(req));
  if (req.tv_sec < 0 || req.tv_nsec < 0)
    return -LINUX_EINVAL;
  return linux_sys_nanosleep(state, req_ptr);
}

static int linux_sys_alarm(uint64_t) {
  return 0;
}

static int linux_sys_rt_sigsuspend(uint64_t, uint64_t) {
  /*
   * Minimal behavior: act as if interrupted immediately.
   * xinit only uses this as part of its legacy wait loop.
   */
  return -LINUX_EINTR;
}

static int linux_sys_setpriority(uint64_t, uint64_t, uint64_t) {
  return 0;
}

static int linux_sys_socket(uint64_t domain, uint64_t type, uint64_t protocol) {
  sock64_init_ops();
  int dom = (int)domain;
  int stype = (int)type & LINUX_SOCK_TYPE_MASK;
  if (dom != LINUX_AF_UNIX)
    return -LINUX_EAFNOSUPPORT;
  if (stype != LINUX_SOCK_STREAM)
    return -LINUX_EPROTONOSUPPORT;
  int idx = sock64_alloc();
  if (idx < 0)
    return -LINUX_ENOMEM;
  linux_socket64_t *s = &g_sock64[idx];
  s->domain = dom;
  s->type = stype;
  s->protocol = (int)protocol;
  return linux_fd_alloc(&s->vnode, (int)type);
}

static int linux_sockaddr_un_path(uint64_t addr_ptr, uint64_t addrlen,
                                  char *out_path, size_t out_sz) {
  if (!addr_ptr || !out_path || out_sz == 0)
    return -LINUX_EFAULT;
  if (addrlen <= OFFSETOF(linux_sockaddr_un_t, sun_path))
    return -LINUX_EINVAL;

  linux_sockaddr_un_t addr;
  memset(&addr, 0, sizeof(addr));
  size_t copy_len = addrlen < sizeof(addr) ? (size_t)addrlen : sizeof(addr);
  memcpy(&addr, (const void *)(uintptr_t)addr_ptr, copy_len);
  if (addr.sun_family != LINUX_AF_UNIX)
    return -LINUX_EINVAL;

  size_t path_len = addrlen - OFFSETOF(linux_sockaddr_un_t, sun_path);
  if (path_len > sizeof(addr.sun_path))
    path_len = sizeof(addr.sun_path);

  memset(out_path, 0, out_sz);
  if (path_len == 0)
    return -LINUX_EINVAL;

  /* Abstract namespace: leading NUL. Convert to "@name" so strcmp works. */
  if (addr.sun_path[0] == '\0') {
    if (path_len <= 1)
      return -LINUX_EINVAL;
    size_t to_copy = path_len - 1;
    if (to_copy > out_sz - 2)
      to_copy = out_sz - 2;
    out_path[0] = '@';
    memcpy(out_path + 1, addr.sun_path + 1, to_copy);
    out_path[1 + to_copy] = 0;
    return 0;
  }

  size_t to_copy = path_len;
  if (to_copy > out_sz - 1)
    to_copy = out_sz - 1;
  memcpy(out_path, addr.sun_path, to_copy);
  out_path[to_copy] = 0;
  return 0;
}

static int linux_sys_bind(uint64_t fd, uint64_t addr_ptr, uint64_t addrlen) {
  file_t *f = syscall64_get_file((int)fd);
  if (!f || !f->node || !f->node->internal)
    return -LINUX_EBADF;
  linux_socket64_t *s = sock64_from_vnode(f->node);
  if (!s)
    return -LINUX_EBADF;
  char path[128];
  int pr = linux_sockaddr_un_path(addr_ptr, addrlen, path, sizeof(path));
  if (pr < 0)
    return pr;
  if (sock64_get_by_path(path))
    return -LINUX_EADDRINUSE;
  strncpy(s->path, path, sizeof(s->path) - 1);
  s->path[sizeof(s->path) - 1] = 0;
  s->state = 1;
  return 0;
}

static int linux_sys_listen(uint64_t fd, uint64_t backlog) {
  file_t *f = syscall64_get_file((int)fd);
  if (!f || !f->node || !f->node->internal)
    return -LINUX_EBADF;
  linux_socket64_t *s = sock64_from_vnode(f->node);
  if (!s || s->state < 1)
    return -LINUX_EINVAL;
  s->state = 2;
  s->backlog = (int)backlog;
  return 0;
}

static int linux_sys_connect(uint64_t fd, uint64_t addr_ptr, uint64_t addrlen) {
  file_t *f = syscall64_get_file((int)fd);
  if (!f || !f->node || !f->node->internal)
    return -LINUX_EBADF;
  linux_socket64_t *s = sock64_from_vnode(f->node);
  if (!s)
    return -LINUX_EBADF;
  char path[128];
  int pr = linux_sockaddr_un_path(addr_ptr, addrlen, path, sizeof(path));
  if (pr < 0)
    return pr;

  linux_socket64_t *listener = sock64_get_by_path(path);
  if (!listener)
    return -LINUX_ECONNREFUSED;
  if (listener->state != 2)
    return -LINUX_ECONNREFUSED;
  int srv_idx = sock64_alloc();
  if (srv_idx < 0)
    return -LINUX_ENOMEM;
  linux_socket64_t *srv = &g_sock64[srv_idx];
  srv->domain = s->domain;
  srv->type = s->type;
  srv->protocol = s->protocol;
  srv->state = 3;
  srv->peer = (int)(s - g_sock64);
  strncpy(srv->path, listener->path, sizeof(srv->path) - 1);
  srv->path[sizeof(srv->path) - 1] = 0;
  s->state = 3;
  s->peer = srv_idx;
  if (sock64_enqueue(listener, srv_idx) < 0)
  {
    s->state = 0;
    s->peer = -1;
    memset(srv, 0, sizeof(*srv));
    return -LINUX_ECONNREFUSED;
  }
  return 0;
}

static int linux_sys_accept(uint64_t fd, uint64_t addr_ptr, uint64_t addrlen) {
  (void)addr_ptr;
  (void)addrlen;
  file_t *f = syscall64_get_file((int)fd);
  if (!f || !f->node || !f->node->internal)
    return -LINUX_EBADF;
  linux_socket64_t *listener = sock64_from_vnode(f->node);
  if (!listener || listener->state != 2)
    return -LINUX_EINVAL;
  int idx = sock64_dequeue(listener);
  while (idx < 0) {
    task64_yield();
    idx = sock64_dequeue(listener);
  }
  linux_socket64_t *s = &g_sock64[idx];
  return linux_fd_alloc(&s->vnode, 0);
}

static int linux_sys_getsockname(uint64_t fd, uint64_t addr_ptr,
                                 uint64_t addrlen_ptr) {
  if (!addr_ptr || !addrlen_ptr)
    return -LINUX_EFAULT;
  file_t *f = syscall64_get_file((int)fd);
  if (!f || !f->node || !f->node->internal)
    return -LINUX_EBADF;
  linux_socket64_t *s = sock64_from_vnode(f->node);
  if (!s)
    return -LINUX_EBADF;
  linux_sockaddr_un_t addr;
  memset(&addr, 0, sizeof(addr));
  addr.sun_family = LINUX_AF_UNIX;
  if (s->path[0] == '@') {
    addr.sun_path[0] = '\0';
    strncpy(addr.sun_path + 1, s->path + 1, sizeof(addr.sun_path) - 2);
  } else {
    strncpy(addr.sun_path, s->path, sizeof(addr.sun_path) - 1);
  }
  *(uint32_t *)(uintptr_t)addrlen_ptr = sizeof(addr);
  return linux_copy_to_user((void *)(uintptr_t)addr_ptr, &addr, sizeof(addr));
}

static int linux_sys_getpeername(uint64_t fd, uint64_t addr_ptr,
                                 uint64_t addrlen_ptr) {
  if (!addr_ptr || !addrlen_ptr)
    return -LINUX_EFAULT;
  file_t *f = syscall64_get_file((int)fd);
  if (!f || !f->node || !f->node->internal)
    return -LINUX_EBADF;
  linux_socket64_t *s = sock64_from_vnode(f->node);
  if (!s || s->peer < 0)
    return -LINUX_EBADF;
  linux_socket64_t *peer = &g_sock64[s->peer];
  linux_sockaddr_un_t addr;
  memset(&addr, 0, sizeof(addr));
  addr.sun_family = LINUX_AF_UNIX;
  if (peer->path[0] == '@') {
    addr.sun_path[0] = '\0';
    strncpy(addr.sun_path + 1, peer->path + 1, sizeof(addr.sun_path) - 2);
  } else {
    strncpy(addr.sun_path, peer->path, sizeof(addr.sun_path) - 1);
  }
  *(uint32_t *)(uintptr_t)addrlen_ptr = sizeof(addr);
  return linux_copy_to_user((void *)(uintptr_t)addr_ptr, &addr, sizeof(addr));
}

static int linux_sys_getsockopt(uint64_t fd, uint64_t level, uint64_t optname,
                                uint64_t optval_ptr, uint64_t optlen_ptr) {
  if (!optval_ptr || !optlen_ptr)
    return -LINUX_EFAULT;

  file_t *f = syscall64_get_file((int)fd);
  if (!f || !f->node || !f->node->internal)
    return -LINUX_EBADF;

  linux_socket64_t *s = sock64_from_vnode(f->node);
  if (!s)
    return -LINUX_EBADF;

  uint32_t optlen = *(uint32_t *)(uintptr_t)optlen_ptr;
  if (optlen < sizeof(int32_t))
    return -LINUX_EINVAL;
  if ((int)level != LINUX_SOL_SOCKET)
    return -LINUX_ENOPROTOOPT;

  int32_t value = 0;
  switch ((int)optname) {
  case LINUX_SO_TYPE:
    value = s->type;
    break;
  case LINUX_SO_ERROR:
    value = 0;
    break;
  default:
    value = 0;
    break;
  }

  if (linux_copy_to_user((void *)(uintptr_t)optval_ptr, &value,
                         sizeof(value)) < 0)
    return -LINUX_EFAULT;
  optlen = sizeof(value);
  if (linux_copy_to_user((void *)(uintptr_t)optlen_ptr, &optlen,
                         sizeof(optlen)) < 0)
    return -LINUX_EFAULT;
  return 0;
}

static int linux_sys_setsockopt(uint64_t fd, uint64_t level, uint64_t optname,
                                uint64_t optval_ptr, uint64_t optlen) {
  (void)optname;
  (void)optval_ptr;
  (void)optlen;

  file_t *f = syscall64_get_file((int)fd);
  if (!f || !f->node || !f->node->internal)
    return -LINUX_EBADF;

  linux_socket64_t *s = sock64_from_vnode(f->node);
  if (!s)
    return -LINUX_EBADF;

  /*
   * xinit/Xlib probes several socket options on AF_UNIX stream sockets.
   * For the current in-kernel Unix socket backend, treating them as successful
   * no-ops is sufficient and matches the behavior expected by the startup path.
   */
  if ((int)level == LINUX_SOL_SOCKET)
    return 0;

  if (s->domain == LINUX_AF_UNIX)
    return 0;

  return -LINUX_ENOPROTOOPT;
}

static int linux_sys_arch_prctl(syscall64_state_t *state, uint64_t code, uint64_t addr) {
  (void)state;
  task64_t *t = task64_current();
  if (!t)
    return -LINUX_ESRCH;
  switch (code) {
  case ARCH_SET_FS:
    t->gs.fs_base = addr;
    wrmsr(MSR_FS_BASE, (uint32_t)addr, (uint32_t)(addr >> 32));
    return 0;
  case ARCH_SET_GS:
    t->gs.user_gs_base = addr;
    wrmsr(MSR_KERNEL_GS_BASE, (uint32_t)addr, (uint32_t)(addr >> 32));
    return 0;
  case ARCH_GET_FS:
    if (!addr)
      return -LINUX_EFAULT;
    *(uint64_t *)(uintptr_t)addr = t->gs.fs_base;
    return 0;
  case ARCH_GET_GS:
    if (!addr)
      return -LINUX_EFAULT;
    *(uint64_t *)(uintptr_t)addr = t->gs.user_gs_base;
    return 0;
  default:
    return -LINUX_EINVAL;
  }
}


static task64_t *task64_clone_current(syscall64_state_t *state, uint64_t child_stack, uint64_t fs_base) {
  task64_t *parent = task64_current();
  if (!parent)
    return nullptr;

  task64_t *child = task64_create(parent->name, parent->entry, parent->arg, TASK64_WAITING);
  if (!child)
    return nullptr;

  child->user_brk_start = parent->user_brk_start;
  child->user_brk_end = parent->user_brk_end;
  child->user_mmap_base = parent->user_mmap_base;
  memcpy(child->vmas, parent->vmas, sizeof(parent->vmas));
  memcpy(child->files, parent->files, sizeof(parent->files));
  memcpy(child->fd_flags, parent->fd_flags, sizeof(parent->fd_flags));
  memcpy(child->cwd, parent->cwd, sizeof(parent->cwd));
  for (int i = 0; i < MAX_FILES_PER_PROCESS; ++i) {
    if (child->files[i])
      child->files[i]->refcount++;
  }

  /* Pending signals are not inherited across fork/clone. */
  child->sig_pending = 0;
  child->sig_mask = parent->sig_mask;
  memcpy(child->sig_actions, parent->sig_actions, sizeof(parent->sig_actions));
  memcpy(child->epoll_table, parent->epoll_table, sizeof(parent->epoll_table));

  if (child_stack)
    child->gs.user_stack = child_stack;
  else
    child->gs.user_stack = parent->gs.user_stack;

  child->gs.fs_base = fs_base;
  child->gs.user_gs_base = parent->gs.user_gs_base;

  if (!state)
    return child;

  uint64_t parent_top = parent->gs.kernel_stack;
  uint64_t parent_rsp = (uint64_t)(uintptr_t)state;
  if (parent_rsp == 0 || parent_rsp >= parent_top)
    return child;

  uint64_t used = parent_top - parent_rsp;
  uint64_t child_top = child->gs.kernel_stack;
  uint64_t child_rsp = child_top - used;

  memcpy((void *)(uintptr_t)child_rsp, (const void *)(uintptr_t)parent_rsp,
         (size_t)used);

  /* Set child RAX to 0 (fork return value for child) */
  syscall64_state_t *st = (syscall64_state_t *)(uintptr_t)child_rsp;
  st->rax = 0;

  /*
   * Prepare stack for switch64:
   * switch64 expects the next stack to look like:
   *   [r15][r14][r13][r12][rbp][rbx][ret]
   * because it pops in that exact order and then returns.
   *
   * After the return, RSP must point at the copied syscall64_state_t so that
   * syscall64_exit restores the child into user mode with RAX=0.
   */
  uint64_t *sp = (uint64_t *)(uintptr_t)child_rsp;
  *(--sp) = (uint64_t)(uintptr_t)syscall64_exit; /* ret */
  *(--sp) = 0; /* rbx */
  *(--sp) = 0; /* rbp */
  *(--sp) = 0; /* r12 */
  *(--sp) = 0; /* r13 */
  *(--sp) = 0; /* r14 */
  *(--sp) = 0; /* r15 */
  child->rsp = (uint64_t)(uintptr_t)sp;

  return child;
}

static int linux_sys_fork(syscall64_state_t *state) {
  task64_t *parent = task64_current();
  if (!parent)
    return -LINUX_ESRCH;

  /* Clone current task — shares address space (vfork-like) */
  uint64_t fs_base = parent->gs.fs_base;
  task64_t *child = task64_clone_current(state, 0, fs_base);
  if (!child)
    return -LINUX_ENOMEM;

  child->parent_id = parent->id;
  child->exit_code = 0;
  child->cr3 = paging64_clone_pml4();
  child->state = TASK64_READY;

  serial_write_string("[LINUX] fork: parent=");
  serial_printf("%u", parent->id);
  serial_write_string(" child=");
  serial_printf("%u", child->id);
  serial_write_string("\r\n");

  /* child's RAX is already set to 0 by task64_clone_current */
  return (int)child->id;
}

#define WNOHANG 1

static int linux_sys_wait4(syscall64_state_t *state, int64_t pid, uint64_t wstatus_ptr, int options) {
  task64_t *parent = task64_current();
  if (!parent)
    return -LINUX_ESRCH;

  serial_write_string("[LINUX] wait4: parent=");
  serial_write_hex(parent->id);
  serial_write_string(" pid=");
  serial_printf("%d", (int)pid);
  serial_write_string("\r\n");

  for (;;) {
    task64_t *head = task64_get_list();
    if (!head)
      return -LINUX_ECHILD;

    task64_t *t = head;
    int has_children = 0;
    do {
      if (t->parent_id == parent->id) {
        has_children = 1;
        
        bool match = false;
        if (pid == -1) match = true;
        else if (pid == 0 && t->gid == parent->gid) match = true;
        else if (pid > 0 && t->id == (uint64_t)pid) match = true;
        
        if (match && t->state == TASK64_ZOMBIE) {
          int child_id = (int)t->id;
          int code = t->exit_code;

          serial_write_string("[LINUX] wait4: reaped child=");
          serial_write_hex((uint64_t)child_id);
          serial_write_string(" exit=");
          serial_printf("%d", code);
          serial_write_string("\r\n");

          if (wstatus_ptr) {
            int32_t wstatus = (int32_t)((code & 0xFF) << 8);
            *(int32_t *)(uintptr_t)wstatus_ptr = wstatus;
          }

          parent->sig_pending &= ~(1ULL << (SIGCHLD - 1));
          t->parent_id = 0;
          t->state = TASK64_UNUSED;
          return child_id;
        }
      }
      t = t->next;
    } while (t && t != head);

    if (!has_children) {
      serial_write_string("[LINUX] wait4: ECHILD (no kids found)\r\n");
      return -LINUX_ECHILD;
    }

    if (options & WNOHANG)
      return 0;

    /* Block until the scheduler wakes us or a timeout/signal path runs. */
    parent->state = TASK64_WAITING;
    syscall64_dispatch_native(state, SYS_SLEEP, 1, 0, 0, 0, 0, 0);
    if (parent->state == TASK64_WAITING)
      parent->state = TASK64_RUNNING;
  }
}

static int64_t linux_syscall_dispatch_x86_64_impl(syscall64_state_t *state, uint64_t num, uint64_t a1, uint64_t a2,
                                  uint64_t a3, uint64_t a4, uint64_t a5,
                                  uint64_t a6) {
  switch (num) {

  case LINUX_NR_exit:
  case LINUX_NR_exit_group: {
    task64_t *t = task64_current();
    if (t)
      t->exit_code = (int)a1;
    return (int64_t)syscall64_dispatch_native(state, SYS_EXIT, a1, 0, 0, 0, 0, 0);
  }

  case LINUX_NR_read:
    return (int64_t)syscall64_dispatch_native(state, SYS_READ, a1, a2, a3, 0, 0, 0);

  case LINUX_NR_write:
    return (int64_t)syscall64_dispatch_native(state, SYS_WRITE, a1, a2, a3, 0, 0, 0);

  case LINUX_NR_socket:
    return linux_sys_socket(a1, a2, a3);

  case LINUX_NR_connect:
    return linux_sys_connect(a1, a2, a3);

  case LINUX_NR_accept:
    return linux_sys_accept(a1, a2, a3);

  case LINUX_NR_bind:
    return linux_sys_bind(a1, a2, a3);

  case LINUX_NR_listen:
    return linux_sys_listen(a1, a2);

  case LINUX_NR_getsockname:
    return linux_sys_getsockname(a1, a2, a3);

  case LINUX_NR_getpeername:
    return linux_sys_getpeername(a1, a2, a3);

  case LINUX_NR_getsockopt:
    return linux_sys_getsockopt(a1, a2, a3, a4, a5);

  case LINUX_NR_setsockopt:
    return linux_sys_setsockopt(a1, a2, a3, a4, a5);

  case LINUX_NR_open:
    return (int64_t)syscall64_dispatch_native(state, SYS_OPEN, a1, a2, 0, 0, 0, 0);

  case LINUX_NR_execve:
  {
    for (int i = 3; i < MAX_FILES_PER_PROCESS; ++i) {
      if (syscall64_get_fd_flags(i) & LINUX_FD_CLOEXEC) {
        syscall64_dispatch_native(state, SYS_CLOSE, (uint64_t)i, 0, 0, 0, 0, 0);
      }
    }
    const char *fname = (const char *)(uintptr_t)a1;
    char *const *argv = (char *const *)(uintptr_t)a2;
    char *const *envp = (char *const *)(uintptr_t)a3;
    if (fname && strstr(fname, "xkbcomp")) {
      file_t *fd0 = syscall64_get_file(0);
      serial_write_string("[LINUX] execve xkbcomp fd0=");
      serial_write_string(fd0 && fd0->node && fd0->node->name ? fd0->node->name : "(none)");
      serial_write_string("\r\n");
    }
    if (fname && fname[0] && !strchr(fname, '/') &&
        (strcmp(fname, "X") == 0 || strcmp(fname, "Xorg") == 0)) {
      int r = execve_linux_x86_64_full("/usr/lib/xorg/Xorg", argv, envp);
      if (r >= 0)
        return r;
      return execve_linux_x86_64_full("/usr/bin/Xorg", argv, envp);
    }
    return execve_linux_x86_64_full(fname, argv, envp);
  }

  case LINUX_NR_stat:
    return linux_sys_stat64(state, (const char *)(uintptr_t)a1,
                            (linux_stat64_t *)(uintptr_t)a2);

  case LINUX_NR_fstat:
    return linux_sys_fstat64(state, (int)a1, (linux_stat64_t *)(uintptr_t)a2);

  case LINUX_NR_lstat:
    return linux_sys_stat64(state, (const char *)(uintptr_t)a1,
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

  case LINUX_NR_openat: {
    int dirfd = (int)a1;
    const char *path = (const char *)(uintptr_t)a2;
    serial_write_string("[K64] openat path=");
    serial_write_string(path ? path : "(null)");
    serial_write_string("\r\n");
    if (path && path[0] == '/') {
      return (int64_t)syscall64_dispatch_native(state, SYS_OPEN, a2, a3, 0, 0, 0, 0);
    }

    if (dirfd == LINUX_AT_FDCWD) {
      return (int64_t)syscall64_dispatch_native(state, SYS_OPEN, a2, a3, 0, 0, 0, 0);
    }
    /* Fallback to normal open for now; full dirfd support requires VFS changes */
    return (int64_t)syscall64_dispatch_native(state, SYS_OPEN, a2, a3, 0, 0, 0, 0);
  }

  case LINUX_NR_chdir:
    return linux_sys_chdir((const char *)(uintptr_t)a1);

  case LINUX_NR_fchdir:
    return linux_sys_fchdir((int)a1);


  case LINUX_NR_close:
    if (a1 >= TASK_LINUX_EPOLL_FD_BASE)
      return epoll64_close(task64_current(), (int)a1);
    return (int64_t)syscall64_dispatch_native(state, SYS_CLOSE, a1, 0, 0, 0, 0, 0);

  case LINUX_NR_fchmod: {
    int fd = (int)a1;
    if (fd < 0 || fd >= MAX_FILES_PER_PROCESS)
      return -LINUX_EBADF;
    file_t *f = syscall64_get_file(fd);
    if (!f)
      return -LINUX_EBADF;
    return 0;
  }

  case LINUX_NR_link: {
    char oldpath[TASK64_CWD_MAX];
    char newpath[TASK64_CWD_MAX];
    int rc = linux_copy_resolved_path((const char *)(uintptr_t)a1, oldpath, sizeof(oldpath));
    if (rc < 0)
      return rc;
    rc = linux_copy_resolved_path((const char *)(uintptr_t)a2, newpath, sizeof(newpath));
    if (rc < 0)
      return rc;
    if (vfs_resolve(newpath))
      return -LINUX_EEXIST;
    if (!vfs_resolve(oldpath))
      return -LINUX_ENOENT;
    int link_rc = ramfs_link_path(oldpath, newpath);
    if (link_rc == -2)
      return -LINUX_EEXIST;
    return link_rc == 0 ? 0 : -LINUX_ENOENT;
  }

  case LINUX_NR_unlink: {
    char path[TASK64_CWD_MAX];
    int rc = linux_copy_resolved_path((const char *)(uintptr_t)a1, path, sizeof(path));
    if (rc < 0)
      return rc;
    vnode_t *node = vfs_resolve(path);
    if (!node || !node->internal)
      return -LINUX_ENOENT;
    return ramfs_unlink_node((FSNode *)node->internal) == 0 ? 0 : -LINUX_EINVAL;
  }

  case LINUX_NR_pipe:
  case LINUX_NR_pipe2: {
    int32_t *fdp = (int32_t *)(uintptr_t)a1;
    if (!fdp)
      return -LINUX_EFAULT;
    if (num == LINUX_NR_pipe2 &&
        (a2 & ~(LINUX_SOCK_NONBLOCK | LINUX_SOCK_CLOEXEC)) != 0)
      return -LINUX_EINVAL;

    pipe_t* p = (pipe_t*)kmalloc(sizeof(pipe_t));
    memset(p, 0, sizeof(*p));
    vnode_t* rnode = pipe_create_vnode(p, 0);
    vnode_t* wnode = pipe_create_vnode(p, 1);
    
    int rfd = linux_fd_alloc(rnode, (int)a2);
    if (rfd < 0)
      return rfd;
    int wfd = linux_fd_alloc(wnode, (int)a2);
    if (wfd < 0) {
      syscall64_dispatch_native(state, SYS_CLOSE, (uint64_t)rfd, 0, 0, 0, 0, 0);
      return wfd;
    }
    
    int32_t fds[2] = {rfd, wfd};
    if (linux_copy_to_user(fdp, fds, sizeof(fds)) < 0) {
      syscall64_dispatch_native(state, SYS_CLOSE, (uint64_t)rfd, 0, 0, 0, 0, 0);
      syscall64_dispatch_native(state, SYS_CLOSE, (uint64_t)wfd, 0, 0, 0, 0, 0);
      return -LINUX_EFAULT;
    }
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
      syscall64_dispatch_native(state, SYS_SLEEP, 1, 0, 0, 0, 0, 0);
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
      return futex64_wait_impl(state, a1, (uint32_t)a3, timeout_ms);
    case FUTEX_WAKE:
      return futex64_wake_impl(a1, (int)a3);
    case FUTEX_WAIT_BITSET:
      return futex64_wait_impl(state, a1, (uint32_t)a3, timeout_ms);
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
      syscall64_dispatch_native(state, SYS_SLEEP, 1, 0, 0, 0, 0, 0);
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
    uint64_t target_id = 0;
    if (num == LINUX_NR_kill || num == LINUX_NR_tkill)
      target_id = a1;
    else
      target_id = a2;

    task64_t *target = nullptr;
    if (target_id > 0)
      target = task64_find_by_id(target_id);
    else
      target = task64_current();

    if (!target)
      return -LINUX_ESRCH;

    if (sig == SIGKILL || sig == SIGTERM) {
      task64_force_exit(target, 128 + sig);
      return 0;
    }

    target->sig_pending |= (1ULL << (sig - 1));
    return 0;
  }

  case LINUX_NR_clone: {
    /* glibc fork() calls clone(SIGCHLD, 0) — no CLONE_VM means fork */
    if (!(a1 & CLONE_VM)) {
      return linux_sys_fork(state);
    }
    uint64_t fs_base = task64_current() ? task64_current()->gs.fs_base : 0;
    if (a1 & CLONE_SETTLS)
      fs_base = a4;
    task64_t *child = task64_clone_current(state, a2, fs_base);
    if (!child)
      return -LINUX_ENOMEM;
    child->parent_id = task64_current() ? task64_current()->id : 0;
    if (a1 & CLONE_PARENT_SETTID && a3)
      *(uint64_t *)(uintptr_t)a3 = child->id;
    if (a1 & CLONE_CHILD_SETTID && a5)
      *(uint64_t *)(uintptr_t)a5 = child->id;
    if (a1 & CLONE_CHILD_CLEARTID)
      child->clear_tid_addr = a5;
    child->state = TASK64_READY;
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
    return linux_sys_arch_prctl(state, a1, a2);

  case LINUX_NR_prctl:
    return linux_sys_prctl(a1, a2, a3, a4, a5);

  case LINUX_NR_brk: {
    uint64_t res = user64_brk(a1);
    return (int64_t)res;
  }

  case LINUX_NR_mmap: {
    serial_write_string("[LINUX] mmap: addr=");
    serial_printf("0x%x", a1);
    serial_write_string(" len=");
    serial_printf("%u", a2);
    serial_write_string(" fd=");
    serial_printf("%d", (int)a5);
    serial_write_string(" off=");
    serial_printf("%u", a6);
    serial_write_string("\r\n");

    if (!(a4 & MAP_ANONYMOUS) && (int)a5 >= 0) {
      file_t *f = syscall64_get_file((int)a5);
      if (f && f->node && f->node->type == VNODE_DEV && f->node->internal) {
        dev_node_t *dn = (dev_node_t *)f->node->internal;
        if (dn->type == DEV_FB0) {
          gpu_device_t *gpu = gpu_get_primary();
          if (!gpu || (a6 & 0xFFFULL))
            return -LINUX_EINVAL;
          uint64_t fb_size = (uint64_t)gpu->pitch * gpu->height;
          if (a6 > fb_size || a2 > fb_size || a6 + a2 > fb_size)
            return -LINUX_EINVAL;
          uint64_t addr = user64_mmap_phys(a1, a2, (int)a3, (int)a4,
                                           gpu->phys_addr + a6);
          if ((int64_t)addr < 0)
            return -LINUX_ENOMEM;
          return (int64_t)addr;
        }
      }
    }

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
    return (int64_t)addr;
  }

  case LINUX_NR_mremap: {
    uint64_t addr = user64_mremap(a1, a2, a3, a4);
    if ((int64_t)addr < 0)
      return -LINUX_ENOMEM;
    return (int64_t)addr;
  }


  case LINUX_NR_munmap:
    if (user64_munmap(a1, a2) < 0)
      return -LINUX_ENOMEM;
    return 0;

  case LINUX_NR_mprotect:
    if (user64_mprotect(a1, a2, (int)a3) < 0)
      return -LINUX_ENOMEM;
    return 0;

  case LINUX_NR_statfs:
    return linux_fill_statfs(a2);

  case 138: /* fstatfs */
    return linux_fill_statfs(a2);

  case LINUX_NR_sched_yield:
    return (int64_t)syscall64_dispatch_native(state, SYS_YIELD, 0, 0, 0, 0, 0, 0);


  case LINUX_NR_pread64: {
    int fd = (int)a1;
    void *buf = (void *)(uintptr_t)a2;
    size_t count = (size_t)a3;
    uint64_t offset = a4;
    file_t *f = syscall64_get_file(fd);
    if (!f || !f->node || !f->node->ops || !f->node->ops->read)
      return -LINUX_EBADF;
    int bytes = f->node->ops->read(f->node, (uint32_t)offset, (uint8_t *)buf, (uint32_t)count);
    return bytes;
  }

  case LINUX_NR_getdents64:
    return linux_sys_getdents64((int)a1, a2, (uint32_t)a3);


  case LINUX_NR_nanosleep:
    return linux_sys_nanosleep(state, a1);

  case LINUX_NR_alarm:
    return linux_sys_alarm(a1);

  case LINUX_NR_clock_nanosleep:
    return linux_sys_clock_nanosleep(state, a1, a2, a3, a4);

  case LINUX_NR_getpid:
    if (task64_current())
      return (int)task64_current()->id;
    return 1;

  case LINUX_NR_getppid:
    return 0;

  case LINUX_NR_getpgrp:
    if (task64_current())
      return (int)task64_current()->id;
    return 1;

  case LINUX_NR_rt_sigsuspend:
    return linux_sys_rt_sigsuspend(a1, a2);

  case LINUX_NR_setpriority:
    return linux_sys_setpriority(a1, a2, a3);

  case LINUX_NR_getuid:
    return task64_current() ? (int)task64_current()->uid : 0;
  case LINUX_NR_getgid:
    return task64_current() ? (int)task64_current()->gid : 0;
  case LINUX_NR_geteuid:
    return task64_current() ? (int)task64_current()->euid : 0;
  case LINUX_NR_getegid:
    return task64_current() ? (int)task64_current()->egid : 0;
  case LINUX_NR_setuid:
    if (task64_current()) task64_current()->uid = (uint32_t)a1;
    return 0;
  case LINUX_NR_setgid:
    if (task64_current()) task64_current()->gid = (uint32_t)a1;
    return 0;

  case LINUX_NR_uname:
    return linux_sys_uname(state, a1);

  case LINUX_NR_set_robust_list:
    if (auto *t = task64_current()) {
      t->robust_list_head = a1;
      t->robust_list_len = a2;
      return 0;
    }
    return -LINUX_ESRCH;

  case LINUX_NR_rseq:
    return -LINUX_ENOSYS;


  case LINUX_NR_sysinfo:
    return linux_sys_sysinfo(state, a1);

  case LINUX_NR_gettimeofday:
    return linux_sys_gettimeofday(state, a1);

  case LINUX_NR_time:
    return linux_sys_time(state, a1);

  case LINUX_NR_clock_gettime:
    return linux_sys_clock_gettime(state, a1, a2);

  case LINUX_NR_clock_getres: {
    if (!a2)
      return -LINUX_EFAULT;
    struct linux_timespec64 ts;
    ts.tv_sec = 0;
    ts.tv_nsec = 1000000; /* 1ms nominal resolution */
    memcpy((void *)(uintptr_t)a2, &ts, sizeof(ts));
    return 0;
  }

  case LINUX_NR_readlink:
    return linux_sys_readlink(state, (const char *)(uintptr_t)a1,
                               (char *)(uintptr_t)a2, (uint32_t)a3);

  case LINUX_NR_umask: {
    uint16_t prev = g_linux_umask;
    g_linux_umask = (uint16_t)(a1 & 0777);
    return prev;
  }

  case LINUX_NR_getresuid: {
    uint32_t *ruid = (uint32_t *)(uintptr_t)a1;
    uint32_t *euid = (uint32_t *)(uintptr_t)a2;
    uint32_t *suid = (uint32_t *)(uintptr_t)a3;
    if (ruid)
      *ruid = 0;
    if (euid)
      *euid = 0;
    if (suid)
      *suid = 0;
    return 0;
  }

  case LINUX_NR_getresgid: {
    uint32_t *rgid = (uint32_t *)(uintptr_t)a1;
    uint32_t *egid = (uint32_t *)(uintptr_t)a2;
    uint32_t *sgid = (uint32_t *)(uintptr_t)a3;
    if (rgid)
      *rgid = 0;
    if (egid)
      *egid = 0;
    if (sgid)
      *sgid = 0;
    return 0;
  }


  case LINUX_NR_access: {
    char path[TASK64_CWD_MAX];
    int rc = linux_copy_resolved_path((const char *)(uintptr_t)a1, path, sizeof(path));
    if (rc < 0)
      return rc;
    vnode_t *node = vfs_resolve(path);
    if (!node)
      return -LINUX_ENOENT;
    return 0;
  }

  case LINUX_NR_newfstatat: {
    int dirfd = (int)a1;
    const char *path = (const char *)(uintptr_t)a2;
    linux_stat64_t *out = (linux_stat64_t *)(uintptr_t)a3;
    if (path && path[0] == '/') {
      return linux_sys_stat64(state, path, out);
    }
    if (dirfd == LINUX_AT_FDCWD)
      return linux_sys_stat64(state, path, out);
    return -LINUX_ENOSYS;
  }

  case LINUX_NR_writev: {
    int fd = (int)a1;
    struct iovec {
      uint64_t base;
      uint64_t len;
    } *iov = (iovec *)(uintptr_t)a2;
    int count = (int)a3;
    if (!iov || count <= 0)
      return -LINUX_EINVAL;
    int total = 0;
    for (int i = 0; i < count; i++) {
      int r = (int)syscall64_dispatch_native(state, SYS_WRITE, (uint64_t)fd,
                                              iov[i].base, iov[i].len, 0, 0, 0);
      if (r < 0)
        return r;
      total += r;
    }
    return total;
  }

  case LINUX_NR_ioctl: {
    int fd = (int)a1;
    uint32_t cmd = (uint32_t)a2;
    void *arg = (void *)(uintptr_t)a3;
    file_t *f = syscall64_get_file(fd);
    if (!f || !f->node || !f->node->ops || !f->node->ops->ioctl)
      return -LINUX_EINVAL;
    int rc = f->node->ops->ioctl(f->node, cmd, arg);
    if (rc < 0)
      return -LINUX_EINVAL;
    return rc;
  }


  case 303: /* name_to_handle_at */
    return -LINUX_ENOSYS;

  case 332: /* statx */
    return -LINUX_ENOSYS;

  case 302: /* prlimit64 */
    return 0;

  case 318: /* getrandom */
    if (a2 > 0) {
        memset((void*)(uintptr_t)a1, 0, (size_t)a2);
    }
    return (int)a2;

  case LINUX_NR_fcntl: {
    int fd = (int)a1;
    int cmd = (int)a2;
    uint64_t arg = a3;
    if (fd < 0 || fd >= MAX_FILES_PER_PROCESS)
      return -LINUX_EBADF;
    file_t *f = syscall64_get_file(fd);
    if (!f)
      return -LINUX_EBADF;
    switch (cmd) {
    case LINUX_F_DUPFD:
    case LINUX_F_DUPFD_CLOEXEC: {
      int start = (int)arg;
      if (start < 0)
        return -LINUX_EINVAL;
      for (int i = start; i < MAX_FILES_PER_PROCESS; ++i) {
        if (!syscall64_get_file(i)) {
          f->refcount++;
          syscall64_set_file(i, f);
          syscall64_set_fd_flags(i, cmd == LINUX_F_DUPFD_CLOEXEC ? LINUX_FD_CLOEXEC : 0);
          return i;
        }
      }
      return -LINUX_ENOMEM;
    }
    case LINUX_F_GETFD:
      return (syscall64_get_fd_flags(fd) & LINUX_FD_CLOEXEC) ? LINUX_FD_CLOEXEC : 0;
    case LINUX_F_SETFD:
      syscall64_set_fd_flags(fd, (arg & LINUX_FD_CLOEXEC) ? LINUX_FD_CLOEXEC : 0);
      return 0;
    case LINUX_F_GETFL:
      return f->flags;
    case LINUX_F_SETFL:
      f->flags = (f->flags & ~(LINUX_O_NONBLOCK | LINUX_O_ASYNC)) |
                 ((int)arg & (LINUX_O_NONBLOCK | LINUX_O_ASYNC));
      return 0;
    case LINUX_F_SETOWN:
      return 0;
    case LINUX_F_GETOWN:
      return 0;
    default:
      return -LINUX_EINVAL;
    }
  }

  case LINUX_NR_dup: {
    int oldfd = (int)a1;
    if (oldfd < 0 || oldfd >= MAX_FILES_PER_PROCESS)
      return -LINUX_EBADF;
    file_t *f = syscall64_get_file(oldfd);
    if (!f) return -LINUX_EBADF;
    for (int i = 3; i < MAX_FILES_PER_PROCESS; i++) {
        if (!syscall64_get_file(i)) {
            f->refcount++;
            syscall64_set_file(i, f);
            syscall64_set_fd_flags(i, 0);
            return i;
        }
    }
    return -LINUX_ENOMEM;
  }

  case LINUX_NR_dup2: {
    int oldfd = (int)a1;
    int newfd = (int)a2;
    if (oldfd < 0 || oldfd >= MAX_FILES_PER_PROCESS)
      return -LINUX_EBADF;
    if (newfd < 0 || newfd >= MAX_FILES_PER_PROCESS)
      return -LINUX_EBADF;
    if (oldfd == newfd)
      return newfd;
    file_t *f = syscall64_get_file(oldfd);
    if (!f)
      return -LINUX_EBADF;
    serial_write_string("[LINUX] dup2 old=");
    serial_printf("%d", oldfd);
    serial_write_string(" new=");
    serial_printf("%d", newfd);
    serial_write_string("\r\n");
    /* Close newfd if open */
    file_t *existing = syscall64_get_file(newfd);
    if (existing) {
      syscall64_dispatch_native(state, SYS_CLOSE, (uint64_t)newfd, 0, 0, 0, 0, 0);
    }
    f->refcount++;
    syscall64_set_file(newfd, f);
    syscall64_set_fd_flags(newfd, 0);
    return newfd;
  }

  case LINUX_NR_dup3: {
    int oldfd = (int)a1;
    int newfd = (int)a2;
    int flags = (int)a3;
    if ((flags & ~LINUX_O_CLOEXEC) != 0)
      return -LINUX_EINVAL;
    if (oldfd < 0 || oldfd >= MAX_FILES_PER_PROCESS)
      return -LINUX_EBADF;
    if (newfd < 0 || newfd >= MAX_FILES_PER_PROCESS)
      return -LINUX_EBADF;
    if (oldfd == newfd)
      return -LINUX_EINVAL;
    file_t *f = syscall64_get_file(oldfd);
    if (!f)
      return -LINUX_EBADF;
    serial_write_string("[LINUX] dup3 old=");
    serial_printf("%d", oldfd);
    serial_write_string(" new=");
    serial_printf("%d", newfd);
    serial_write_string(" flags=");
    serial_printf("%d", flags);
    serial_write_string("\r\n");
    file_t *existing = syscall64_get_file(newfd);
    if (existing) {
      syscall64_dispatch_native(state, SYS_CLOSE, (uint64_t)newfd, 0, 0, 0, 0, 0);
    }
    f->refcount++;
    syscall64_set_file(newfd, f);
    syscall64_set_fd_flags(newfd, (flags & LINUX_O_CLOEXEC) ? LINUX_FD_CLOEXEC : 0);
    return newfd;
  }

  case LINUX_NR_fork:
  case LINUX_NR_vfork:
    return linux_sys_fork(state);

  case LINUX_NR_wait4:
    return linux_sys_wait4(state, (int64_t)a1, a2, (int)a3);

  case LINUX_NR_waitid:
    /* waitid(idtype, id, infop, options, rusage) */
    /* Minimal stub: redirect to wait4(id, infop, options) if idtype matches */
    return linux_sys_wait4(state, (int64_t)a2, a3, (int)a4);

  default:
    serial_write_string("[K64] unknown linux syscall rax=");
    serial_printf("%u", num);
    serial_write_string("\r\n");
    return -LINUX_ENOSYS;
  }
}

extern "C" int64_t linux_syscall_dispatch_x86_64(void* state_ptr, uint64_t num, uint64_t a1, uint64_t a2,
                                  uint64_t a3, uint64_t a4, uint64_t a5,
                                  uint64_t a6) {
  syscall64_state_t* state = (syscall64_state_t*)state_ptr;
  signal64_dispatch(task64_current(), state);

  task64_t *t = task64_current();
  serial_write_string("[LINUX] Task ");
  serial_write_hex(t ? t->id : 0);
  serial_write_string(" syscall rax=");
  serial_printf("%d", (int)num);
  serial_write_string("\r\n");

  int64_t ret = linux_syscall_dispatch_x86_64_impl(state, num, a1, a2, a3, a4, a5, a6);

  serial_write_string("[LINUX] syscall rax=");
  serial_printf("%u", num);
  serial_write_string(" ret=");
  serial_printf("%d", (int)ret);
  serial_write_string("\r\n");

  return ret;
}




#else

int64_t linux_syscall_dispatch_x86_64(void* state, uint64_t num, uint64_t a1, uint64_t a2,
                                      uint64_t a3, uint64_t a4, uint64_t a5,
                                      uint64_t a6) {
  (void)state;
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
