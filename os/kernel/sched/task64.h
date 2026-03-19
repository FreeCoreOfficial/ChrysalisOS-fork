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
  TASK64_ZOMBIE
} task64_state_t;

typedef struct task64 {
  uint64_t id;
  char name[32];
  uint64_t rsp;
  struct {
    uint64_t kernel_stack;
    uint64_t user_stack;
  } gs;
  uint64_t user_brk_start;
  uint64_t user_brk_end;
  uint64_t user_mmap_base;
  void (*entry)(void *arg);
  void *arg;
  task64_state_t state;
  struct task64 *next;
} task64_t;

void task64_init(void);
task64_t *task64_create(const char *name, void (*entry)(void *), void *arg);
void task64_start(task64_t *first);
void task64_yield(void);
task64_t *task64_current(void);
void task64_set_user_stack(uint64_t user_rsp);

#ifdef __cplusplus
}
#endif
