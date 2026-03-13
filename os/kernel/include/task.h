#pragma once
#include <stdint.h>

#define KSTACK_SIZE 16384

#include "../fs/vfs/vfs.h"
#include "../input/input.h"

#ifdef __cplusplus
extern "C" {
#endif

#ifndef __TASK_T_DEFINED_BY_HEADER
#define __TASK_T_DEFINED_BY_HEADER

/* Primary task function type used by the scheduler implementation:
 * takes a single void* argument (can be NULL).
 */
typedef void (*task_fn_t)(void *arg);

/* For convenience, also define the common no-arg form */
typedef void (*task_fn_noarg_t)(void);

typedef enum {
  TASK_UNUSED = 0,
  TASK_READY,
  TASK_RUNNING,
  TASK_SLEEPING,
  TASK_ZOMBIE
} task_state_t;

#define TASK_EVENT_QUEUE_SIZE 32

typedef struct task {
  int pid;
  char name[32];
  void (*entry_noarg)(void); /* saved entry point; used by task trampoline */
  uint8_t is_user_app;       /* dynamically loaded .petal task */
  char launch_arg[256];      /* argv[1] passed by execve for standalone apps */
  uint32_t
      *kstack_ptr; /* pointer la frame-ul salvat (folosit de context_switch) */
  uint32_t cr3;    /* optional: pagina director (setează CR3 la switch) */
  uint32_t kernel_stack;
  uint32_t user_stack;
  uint32_t last_syscall;
  uint32_t last_syscall_a1;
  uint32_t last_syscall_a2;
  uint32_t last_syscall_a3;

  /* PCB Compatibility Fields */
  task_state_t state;
  uint32_t ticks_remaining;
  file_t *files[MAX_FILES_PER_PROCESS];

  /* Process-wide Event Queue */
  input_event_t event_queue[TASK_EVENT_QUEUE_SIZE];
  int event_head;
  int event_tail;

  uint8_t kstack[KSTACK_SIZE] __attribute__((aligned(16)));
  struct task *next; /* pentru scheduler circular simplu */

  /* New fields for state-aware scheduler */
  uint64_t sleep_until;
} task_t;

/* Helper to push event to task queue */
void task_push_event(task_t *t, input_event_t *ev);
/* Helper to pop event from task queue */
int task_pop_event(task_t *t, input_event_t *ev);
void task_kill_user_apps(void);
#endif

/* API minim */
task_t *task_create(void (*entry)(void), int pid);
task_t *task_create_name(const char *name, void (*entry)(void));

void task_init_scheduler(void);
void task_init(void);

void yield(void);      /* forțează context switch */
void task_yield(void); /* alias for yield */
void schedule(void);
void task_exit(int code);
extern task_t *current_task;

#ifdef __cplusplus
}
#endif
