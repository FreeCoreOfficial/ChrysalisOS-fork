#pragma once
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
  TASK64_UNUSED = 0,
  TASK64_READY,
  TASK64_RUNNING,
  TASK64_SLEEPING,
  TASK64_WAITING,
  TASK64_ZOMBIE
} task64_state_t;

typedef struct task64 {
  uint64_t id;
  char name[32];
  uint64_t rsp;
  struct {
    uint64_t kernel_stack;
    uint64_t user_stack;
    uint64_t fs_base;
    uint64_t user_gs_base;
  } gs;
  void *kernel_stack_base;
  uint64_t user_brk_start;
  uint64_t user_brk_end;
  uint64_t user_mmap_base;
  struct {
    uint64_t start;
    uint64_t end;
    int prot;
    int flags;
    int used;
  } vmas[128];
  void (*entry)(void *arg);
  void *arg;
  task64_state_t state;
  struct task64 *next;

  uint64_t sig_pending;
  uint64_t sig_mask;
  struct {
    void (*handler)(int);
    uint32_t flags;
    uint64_t mask;
  } sig_actions[64];
  uint64_t sig_saved_rip;
  uint64_t sig_saved_rsp;
  int sig_active;

  uint64_t clear_tid_addr;

  void *epoll_table[32];
  char exe_path[128];

  uint32_t uid, gid;
  uint32_t euid, egid;
  uint64_t sleep_until;

  uint64_t parent_id;
  int exit_code;
  uint64_t cr3;
} task64_t;


void task64_init(void);
task64_t *task64_create(const char *name, void (*entry)(void *), void *arg, task64_state_t initial_state);
void task64_start(task64_t *first);
void task64_yield(void);
task64_t *task64_current(void);
void task64_set_user_stack(uint64_t user_rsp);
task64_t *task64_find_by_id(uint64_t id);
task64_t *task64_get_list(void);

#ifdef __cplusplus
}
#endif
