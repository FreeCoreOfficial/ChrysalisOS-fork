/* kernel/sched/scheduler.c */
#include "scheduler.h"
#include "../terminal.h" /* optional */
#include "pcb.h"
#include <stdint.h>

/* ---- defensive declarations for helpers provided by pcb.c ----
 * Ensure these prototypes exist (they're defined in pcb.c). If your pcb.h
 * hasn't these, having extern prototypes here avoids implicit-declaration
 * compile errors.
 */
extern pcb_t *_pcb_get_by_index(int idx);
extern int _pcb_count(void);
extern void _pcb_set_current(int idx);
extern int _pcb_get_current_idx(void);

/* CONFIG */
static uint32_t QUANTUM_TICKS = 2; /* default (change via scheduler_init) */

/* runtime enabled flag - if something goes wrong we set this to 0 so
 * the scheduler becomes a no-op (fallback).
 */
static volatile int scheduler_enabled = 0;

#ifndef SCHED_COOPERATIVE
/* preemptive: this variable set by PIT IRQ handler (scheduler_tick),
 * scheduler checks it and switches when needed.
 */
static volatile int need_reschedule = 0;
#endif

/* forward: context switch primitive */
static void context_switch(uint32_t **old_esp, uint32_t *new_esp);

/* helper trampoline (assembly wrapper) - called when a fresh task is started */
extern void task_trampoline(void);

/* round robin queue simple pointer */
static int last_run = -1;

void scheduler_init(uint32_t quantum_ticks) {
  QUANTUM_TICKS = quantum_ticks ? quantum_ticks : QUANTUM_TICKS;
  pcb_init_all();
  last_run = -1;

  /* basic sanity: if there are no pcb slots, disable scheduler (fallback) */
  if (_pcb_count() <= 0) {
    terminal_writestring(
        "[sched] no PCB slots available -> scheduler disabled (fallback)\n");
    scheduler_enabled = 0;
  } else {
    scheduler_enabled = 1;
  }
}

/* Called by PIT IRQ handler every tick */
void scheduler_tick(void) {
  if (!scheduler_enabled)
    return;
#ifndef SCHED_COOPERATIVE
  /* set flag: reschedule required at next safe point (end of IRQ) */
  need_reschedule = 1;
  /* Optionally: decrement current's remaining ticks here to force switch */
  pcb_t *cur = pcb_get_current();
  if (cur) {
    if (cur->ticks_remaining > 0)
      cur->ticks_remaining--;
    if (cur->ticks_remaining == 0)
      need_reschedule = 1;
  }
#endif
}

/* yield (cooperative) or voluntary yield call */
void scheduler_yield(void) {
  if (!scheduler_enabled)
    return;

  /* mark current as ready and force reschedule */
  pcb_t *cur = pcb_get_current();
  if (cur) {
    cur->state = TASK_READY;
  }
#ifndef SCHED_COOPERATIVE
  need_reschedule = 1;
  /* call schedule immediately (safe to call from code path) */
#endif

  /* do scheduling now */
  int start = last_run;
  int found = -1;
  for (int i = 1; i <= MAX_TASKS; ++i) {
    int idx = (start + i) % MAX_TASKS;
    pcb_t *p = _pcb_get_by_index(idx);
    if (!p)
      continue;
    if (p->state == TASK_READY) {
      found = idx;
      break;
    }
  }
  if (found == -1) {
    /* nothing to run -> fallback: leave scheduler enabled but do nothing */
    return;
  }

  /* perform switch */
  int prev = _pcb_get_current_idx();
  pcb_t *prev_p = _pcb_get_by_index(prev);
  pcb_t *next_p = _pcb_get_by_index(found);

  if (!next_p) {
    /* defensive: invalid next pointer -> disable scheduler as precaution */
    terminal_writestring(
        "[sched] invalid next_p -> disabling scheduler (fallback)\n");
    scheduler_enabled = 0;
    return;
  }

  if (prev_p)
    prev_p->state = TASK_READY;
  next_p->state = TASK_RUNNING;
  next_p->ticks_remaining = QUANTUM_TICKS;
  _pcb_set_current(found);
  last_run = found;

  if (prev_p) {
    context_switch(&prev_p->kstack_ptr, next_p->kstack_ptr);
  } else {
    /* no previous (first run) */
    uint32_t *dummy = 0;
    context_switch(&dummy, next_p->kstack_ptr);
  }
}

/* internal schedule used by IRQ handler: called from end of IRQ or tick path.
 * This is kept minimal: it only swaps if need_reschedule is set.
 */
void scheduler_handle_reschedule_if_needed(void) {
  if (!scheduler_enabled)
    return;
#ifndef SCHED_COOPERATIVE
  if (!need_reschedule)
    return;
  need_reschedule = 0;

  /* find next ready task */
  int start = last_run;
  int found = -1;
  for (int i = 1; i <= MAX_TASKS; ++i) {
    int idx = (start + i) % MAX_TASKS;
    pcb_t *p = _pcb_get_by_index(idx);
    if (!p)
      continue;
    if (p->state == TASK_READY) {
      found = idx;
      break;
    }
  }
  if (found == -1)
    return;

  int prev = _pcb_get_current_idx();
  pcb_t *prev_p = _pcb_get_by_index(prev);
  pcb_t *next_p = _pcb_get_by_index(found);

  if (!next_p) {
    terminal_writestring("[sched] invalid next_p in IRQ handler -> disabling "
                         "scheduler (fallback)\n");
    scheduler_enabled = 0;
    return;
  }

  if (prev_p)
    prev_p->state = TASK_READY;
  next_p->state = TASK_RUNNING;
  next_p->ticks_remaining = QUANTUM_TICKS;
  _pcb_set_current(found);
  last_run = found;

  if (prev_p) {
    context_switch(&prev_p->kstack_ptr, next_p->kstack_ptr);
  } else {
    uint32_t *dummy = 0;
    context_switch(&dummy, next_p->kstack_ptr);
  }
#endif
}

/* Start scheduler — call from kernel_main once tasks created.
 * If you have an idle loop, you can enter it here.
 */
void scheduler_start(void) {
  if (!scheduler_enabled) {
    terminal_writestring("[sched] scheduler_start called but scheduler "
                         "disabled -> returning to kernel\n");
    return;
  }

  /* find first ready task */
  for (int i = 0; i < MAX_TASKS; ++i) {
    pcb_t *p = _pcb_get_by_index(i);
    if (!p)
      continue;
    if (p->state == TASK_READY) {
      p->state = TASK_RUNNING;
      p->ticks_remaining = QUANTUM_TICKS;
      _pcb_set_current(i);
      last_run = i;
      uint32_t *dummy = 0;
      context_switch(&dummy, p->kstack_ptr);
      /* never returns unless task switches back */
      return;
    }
  }

  /* nothing to run: fallback -> disable scheduler and return to kernel_main
   * (idle) */
  terminal_writestring(
      "[sched] no ready tasks -> scheduler disabled (fallback)\n");
  scheduler_enabled = 0;
}

/* ---------- Context switch primitive (inline asm) ----------
 * We save the current ESP into *old_esp and load new_esp into ESP.
 * We wrap with pushad/popad so registers remain intact across switch.
 *
 * Function prototype: static void context_switch(uint32_t **old_esp, uint32_t
 * *new_esp);
 *
 * Note: this is a minimal primitive and assumes that stacks of tasks
 * are prepared in pcb_create to contain a return address that leads
 * to task_trampoline; see pcb.c.
 */
static void context_switch(uint32_t **old_esp, uint32_t *new_esp) {
  asm volatile("pushal\n\t"           /* save registers (EAX..EDI) */
               "movl %%esp, (%0)\n\t" /* store current esp into *old_esp */
               "movl %1, %%esp\n\t"   /* load new esp */
               "popal\n\t"            /* restore registers for new task */
               :
               : "r"(old_esp), "r"(new_esp)
               : "memory");
}

/* ---------- task_trampoline implementation ----------
 * This function is where new tasks begin executing. It pulls the
 * current pcb, calls its entry, and when the task returns it marks
 * the task as ZOMBIE and yields.
 */
void task_trampoline_c(void) {
  pcb_t *cur = pcb_get_current();
  if (!cur) {
    /* nothing */
    for (;;)
      ; /* halt */
  }
  /* Note: entry and arg are no longer fields in task_t.
   * They are prepared on the stack during task creation.
   * The task function will be called via the return from context_switch.
   * When it returns here, we mark it as ZOMBIE.
   */
  /* task returned -> mark ZOMBIE and yield */
  cur->state = TASK_ZOMBIE;
  scheduler_yield();
  for (;;)
    ;
}

/* create an assembly label for trampoline that calls C function above */
__attribute__((naked)) void task_trampoline(void) {
  asm volatile("pusha\n\t"
               "call task_trampoline_c\n\t"
               "popa\n\t"
               "ret\n\t");
}
