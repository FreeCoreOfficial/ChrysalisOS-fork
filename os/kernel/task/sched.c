/* kernel/task/sched.c
 *
 * Defensive scheduler fallback (weak symbols).
 *
 * This file marks its public functions as weak so that if another translation
 * unit (e.g. task.c fallback) provides a strong implementation linker will
 * prefer the strong one and there will be no multiple-definition error.
 *
 * Behavior:
 * - If you provide an assembly context switch named `switch_to(prev,next)`
 *   we call it. If you provide
 * `context_switch(&prev->kstack_ptr,&next->kstack_ptr)` we call that. If
 * neither exists, we advance current_task only (bookkeeping).
 */

#include "../include/task.h"
#include <stddef.h>

/* External references (may be provided elsewhere) */
extern task_t *current_task; /* provided by task implementation or fallback */

/* Optional assembly routines (weak): */
extern void switch_to(task_t *prev, task_t *next) __attribute__((weak));
extern void context_switch(void *prev_kstack_ptr_addr,
                           void *next_kstack_ptr_addr) __attribute__((weak));

/* Weak schedule implementation: if a strong one exists elsewhere linker picks
 * it. */
void schedule(void) __attribute__((weak));
void schedule(void) {
  if (!current_task)
    return;
  task_t *prev = current_task;
  task_t *next = prev->next;

  if (!next || next == prev)
    return; /* nothing to do or malformed list */

  /* Prefer an asm-level switch if available */
  if (switch_to) {
    /* update bookkeeping before calling switch */
    current_task = next;
    switch_to(prev, next);
    return;
  }

  if (context_switch) {
    context_switch(&prev->kstack_ptr, &next->kstack_ptr);
    current_task = next;
    return;
  }

  /* No real context switch available: just advance pointer for bookkeeping. */
  current_task = next;
}

/* Weak yield wrappers */
void yield(void) __attribute__((weak));
void yield(void) { schedule(); }

void task_yield(void) __attribute__((weak));
void task_yield(void) { schedule(); }

/* Small helper so runtime can detect fallback scheduler if needed (weak) */
int scheduler_is_fallback(void) __attribute__((weak));
int scheduler_is_fallback(void) {
  return (switch_to == NULL && context_switch == NULL) ? 1 : 0;
}
