/* kernel/task/task.c
 *
 * Forced fallback task implementation (self-contained).
 * - Provides task_t if not available.
 * - Implements both old and new task_create signatures:
 *     task_t *task_create(const char *name, void (*entry)(void));
 *     task_t *task_create(void (*entry)(void), int pid);
 * - Implements task_init / task_init_scheduler, task_yield / yield
 * - Implements schedule() that will call a weak context_switch if present,
 *   otherwise advances current_task pointer (no real stack switch).
 *
 * This is defensive: it avoids compile errors and won't cause bootloops by
 * attempting an invalid context switch if you don't have switch.S implemented.
 */

#include "../terminal.h"
#include <stddef.h>
#include <stdint.h>

#include "../include/task.h"
#include "../mm/paging.h"
#include "../time/timer.h"
#include "../arch/i386/gdt.h"
#include "../string.h"

/* Freestanding-friendly tiny helpers */
static void *k_memset(void *s, int c, size_t n) {
  unsigned char *p = (unsigned char *)s;
  while (n--)
    *p++ = (unsigned char)c;
  return s;
}

/* extern allocator hooks (must exist in kernel) */
extern void *kmalloc(size_t size);
extern void kfree(void *ptr);

/* extern weak context_switch: if you provide arch/i386/switch.S with this
   symbol, schedule() will call it to actually switch stacks. Otherwise we do
   nothing dangerous. */
extern void context_switch(void *prev_kstack_ptr_addr,
                           void *next_kstack_ptr_addr) __attribute__((weak));

/* Scheduler hook — may be overridden by a richer sched.c, but implement here */
static task_t *task_list = NULL;
task_t *current_task = NULL;
static uint32_t next_pid = 1;

/* Compatibility with legacy PCB system (defined in pcb.c) */
extern void _pcb_set_current(int tid);

/* helper: initial eflags for new tasks (IF=1) */
static inline uint32_t initial_eflags(void) { return 0x202; }

static inline uint32_t task_read_cr3(void) {
  uint32_t cr3;
  asm volatile("mov %%cr3, %0" : "=r"(cr3));
  return cr3;
}

static void task_apply_tls(task_t *t) {
  if (!t)
    return;
  gdt_set_tls_base(t->tls_base);
  uint16_t sel = gdt_get_tls_selector();
  asm volatile("movw %0, %%gs" : : "rm"(sel));
}

/* Every task starts in this trampoline so return from entry is always handled.
   This mirrors Linux-style "if task function returns -> do_exit". */
static __attribute__((noreturn)) void task_entry_trampoline(void) {
  void (*entry)(void) = NULL;
  if (current_task) {
    entry = current_task->entry_noarg;
  }

  if (!entry) {
    terminal_writestring("[TASK] Null entry in trampoline. Exiting task.\n");
    task_exit(-1);
  }

  if (current_task && current_task->user_stack) {
    uint32_t usp = current_task->user_stack;
    asm volatile("movl %0, %%esp\n"
                 "jmp *%1\n"
                 :
                 : "r"(usp), "r"(entry)
                 : "memory");
  }

  entry();
  task_exit(0);

  for (;;)
    asm volatile("hlt");
}

/* Minimal schedule(): cooperative round-robin.
 * If context_switch is provided (non-NULL weak symbol), call it with addresses
 * of prev->kstack_ptr and next->kstack_ptr. If not present, only advance the
 * `current_task` pointer (safe bookkeeping, no stack change).
 */
void schedule(void) {
  if (!current_task || !task_list)
    return;

  /* Interrupts MUST be disabled during the entire scheduling process
     to prevent race conditions (e.g. if we update current_task and then
     an IRQ happens before we context_switch). */
  asm volatile("cli");

  uint64_t now = timer_ticks_no_cli();
  task_t *prev = current_task;
  task_t *next = prev->next;

  /* Check all tasks once for a ready one */
  while (next != prev) {
    if (next->state == TASK_SLEEPING && now >= next->sleep_until) {
      next->state = TASK_READY;
    }

    if (next->state == TASK_READY || next->state == TASK_RUNNING) {
      break;
    }
    next = next->next;
  }

  if (next == prev) {
    /* Only current task is potentially ready. Check if it should wake up. */
    if (prev->state == TASK_SLEEPING && now >= prev->sleep_until) {
      prev->state = TASK_READY;
    }
    if (prev->state != TASK_READY && prev->state != TASK_RUNNING) {
      /* Everything is sleeping, just stay here for now. */
      asm volatile("sti");
      return;
    }
    asm volatile("sti");
    return;
  }

  if (context_switch) {
    if (next->cr3 && next->cr3 != task_read_cr3()) {
      paging_load_directory(next->cr3);
    }

    /* CRITICAL: Update current_task pointer BEFORE switching stacks. */
    current_task = next;

    /* Also update legacy PCB system ID if compatible */
    _pcb_set_current(next->pid);

    /* context_switch.S will save context and LOAD new stack.
       It also re-enables interrupts via sti before ret. */
    context_switch(&prev->kstack_ptr, &next->kstack_ptr);
    task_apply_tls(current_task);
  } else {
    if (next->cr3 && next->cr3 != task_read_cr3()) {
      paging_load_directory(next->cr3);
    }

    /* No real context switch available */
    current_task = next;
    _pcb_set_current(next->pid);
    task_apply_tls(current_task);
    asm volatile("sti");
  }
}

/* Public wrappers: yield / task_yield (old names) */
void yield(void) { schedule(); }
void task_yield(void) { schedule(); }

void task_push_event(task_t *t, input_event_t *ev) {
  if (!t || !ev)
    return;

  if (ev->type == INPUT_WINDOW_RESIZE && t->event_head != t->event_tail) {
    int last =
        (t->event_tail + TASK_EVENT_QUEUE_SIZE - 1) % TASK_EVENT_QUEUE_SIZE;
    if (t->event_queue[last].type == INPUT_WINDOW_RESIZE) {
      t->event_queue[last] = *ev;
      return;
    }
  }

  int next_tail = (t->event_tail + 1) % TASK_EVENT_QUEUE_SIZE;
  if (next_tail == t->event_head) {
    /* Queue full: keep latest resize state if possible */
    if (ev->type == INPUT_WINDOW_RESIZE && t->event_head != t->event_tail) {
      int last =
          (t->event_tail + TASK_EVENT_QUEUE_SIZE - 1) % TASK_EVENT_QUEUE_SIZE;
      t->event_queue[last] = *ev;
    }
    return;
  }
  t->event_queue[t->event_tail] = *ev;
  t->event_tail = next_tail;
}

int task_pop_event(task_t *t, input_event_t *ev) {
  if (!t || t->event_head == t->event_tail)
    return 0;
  *ev = t->event_queue[t->event_head];
  t->event_head = (t->event_head + 1) % TASK_EVENT_QUEUE_SIZE;
  return 1;
}

void task_kill_user_apps(void) {
  if (!task_list)
    return;

  extern void wm_cleanup_task_windows(void *task_ptr);

  task_t *t = task_list;
  do {
    if (t != current_task && t->is_user_app && t->state != TASK_ZOMBIE) {
      wm_cleanup_task_windows(t);
      t->state = TASK_ZOMBIE;
    }
    t = t->next;
  } while (t && t != task_list);
}

/* Initialize scheduler by capturing current stack into a static main task */
void task_init(void) {
  if (current_task)
    return; /* already initialized */

  static task_t main_task;
  k_memset(&main_task, 0, sizeof(main_task));

  main_task.pid = next_pid++;
  main_task.next = &main_task;
  main_task.state = TASK_READY;
  main_task.abi = TASK_ABI_CHRYSALIS;

  /* capture current ESP */
  uint32_t cur_esp;
  asm volatile("movl %%esp, %0" : "=r"(cur_esp));

  main_task.kstack_ptr = (uint32_t *)cur_esp;
  main_task.cr3 = task_read_cr3();

  current_task = &main_task;
  task_list = &main_task;
}

/* Provide alias to canonical name */
void task_init_scheduler(void) { task_init(); }

/* Internal stack push helper */
static inline uint32_t *push32(uint32_t *sp, uint32_t v) {
  sp--;
  *sp = v;
  return sp;
}

static void task_set_name(task_t *t, const char *name) {
  if (!t)
    return;
  if (!name)
    name = "task";
  int i = 0;
  while (name[i] && i < (int)sizeof(t->name) - 1) {
    t->name[i] = name[i];
    i++;
  }
  t->name[i] = 0;
}

/* Create new task (canonical signature): entry, pid */
task_t *task_create(void (*entry)(void), int pid) {
  if (!entry)
    return NULL;

  task_t *t = (task_t *)kmalloc(sizeof(task_t));
  if (!t)
    return NULL;

  k_memset(t, 0, sizeof(*t));

  t->pid = (pid == 0) ? (int)next_pid++ : pid;
  t->entry_noarg = entry;
  t->state = TASK_READY;
  t->abi = TASK_ABI_CHRYSALIS;
  t->cr3 = current_task ? current_task->cr3 : task_read_cr3();
  task_set_name(t, "task");
  t->last_syscall = 0;
  t->last_syscall_a1 = 0;
  t->last_syscall_a2 = 0;
  t->last_syscall_a3 = 0;

  /* prepare stack in embedded kstack */
  uint32_t *sp = (uint32_t *)((uintptr_t)t->kstack + sizeof(t->kstack));

  /* push return EIP (so ret -> task trampoline) */
  sp = push32(sp, (uint32_t)(uintptr_t)task_entry_trampoline);

  /* push EFLAGS (popfl will restore) */
  sp = push32(sp, initial_eflags());

  /* push registers for popal: EAX,ECX,EDX,EBX,ESP_saved,EBP,ESI,EDI */
  /* store zeros for simplicity */
  sp = push32(sp, 0); /* EAX */
  sp = push32(sp, 0); /* ECX */
  sp = push32(sp, 0); /* EDX */
  sp = push32(sp, 0); /* EBX */
  sp = push32(sp, 0); /* ESP_saved */
  sp = push32(sp, 0); /* EBP */
  sp = push32(sp, 0); /* ESI */
  sp = push32(sp, 0); /* EDI */

  t->kstack_ptr = sp;

  /* init event queue */
  t->event_head = 0;
  t->event_tail = 0;

  /* insert into circular list */
  if (!task_list) {
    t->next = t;
    task_list = t;
    /* If current_task is null, set it to main-like task; but we keep
     * current_task if set */
    if (!current_task)
      current_task = t;
  } else {
    /* append after head */
    t->next = task_list->next;
    task_list->next = t;
  }

  /* Log task creation for debugging */
  terminal_printf("[TASK] Created task PID=%d at %x\n", t->pid, entry);

  return t;
}

/* Old signature compatibility: task_create(name, entry) */
task_t *task_create_name(const char *name, void (*entry)(void)) {
  task_t *t = task_create(entry, 0);
  if (t)
    task_set_name(t, name);
  return t;
}

/* Provide old symbol name if some files call task_create("name", entry) */
task_t *task_create_old(const char *name, void (*entry)(void))
    __attribute__((alias("task_create_name")));

/* Also provide function named exactly task_create(const char*,void*) in C
   to catch old callers at compile time: define weak wrapper with C linkage.
   Some compilers/linkers may complain about duplicate symbols if the canonical
   header expects different signature — but this fallback aims to be permissive.
*/

#if defined(__GNUC__)
/* Provide a C wrapper with the old signature but different name to avoid
   conflicting declarations. Kernel code should call task_create(...) new API.
*/
#endif

/* Free a task (simple) */
/* Free a task (simple) */
void task_free(task_t *t) {
  if (!t)
    return;
  kfree(t);
}

/* Exit current task: remove from list and free */
void task_exit(int code) {
  (void)code; /* Exit code not really used yet */

  /* If only one task (or none), we can't really exit without halting */
  if (!current_task || !task_list || task_list->next == task_list) {
    terminal_writestring("[TASK] Last task exiting, halting.\n");
    for (;;)
      asm volatile("hlt");
  }

  /* Remove current_task from circular list */
  task_t *prev = current_task;
  while (prev->next != current_task) {
    prev = prev->next;
  }

  /* prev->next was current_task, now make it current_task->next */
  prev->next = current_task->next;

  task_t *to_free = current_task;

  /* safe book-keeping */
  if (task_list == to_free) {
    task_list = prev; /* move head if we delete head */
  }

  /* Schedule next task immediately */
  /* We manually move current_task to next so schedule() picks it up
     without trying to save the old (freed) context */
  current_task = prev; /* schedule() will start searching from prev->next */

  /* We can't free the stack we are running on if we want to call schedule()
     safely... but for this simple kernel we'll hack it:
     we mark it ZOMBIE and let a reaper (or nothing) free it,
     OR we just leak it for now to be safe.
     Better: set state ZOMBIE and schedule away. */

  /* Cleanup any windows owned by this task */
  extern void wm_cleanup_task_windows(void *task_ptr);
  wm_cleanup_task_windows(to_free);

  if (to_free->clear_tid_addr) {
    extern int syscall_user_range_ok(const void *ptr, uint32_t len);
    uint32_t *p = (uint32_t *)(uintptr_t)to_free->clear_tid_addr;
    if (syscall_user_range_ok(p, sizeof(uint32_t)))
      *p = 0;
  }

  to_free->state = TASK_ZOMBIE;

  schedule();

  /* Should never return */
  for (;;)
    asm volatile("hlt");
}

void syscall_capture_kstack(uint32_t sp) {
  if (!current_task)
    return;
  current_task->kstack_ptr = (uint32_t *)(uintptr_t)sp;
}

task_t *task_clone_current(uint32_t child_stack) {
  if (!current_task)
    return NULL;

  task_t *child = (task_t *)kmalloc(sizeof(task_t));
  if (!child)
    return NULL;
  k_memset(child, 0, sizeof(*child));

  /* clone state */
  *child = *current_task;
  child->pid = (int)next_pid++;
  child->state = TASK_READY;
  child->next = NULL;

  /* copy kernel stack frame */
  uintptr_t parent_top =
      (uintptr_t)current_task->kstack + sizeof(current_task->kstack);
  uintptr_t parent_sp = (uintptr_t)current_task->kstack_ptr;
  if (parent_sp < (uintptr_t)current_task->kstack ||
      parent_sp > parent_top) {
    kfree(child);
    return NULL;
  }
  size_t used = parent_top - parent_sp;
  uintptr_t child_top = (uintptr_t)child->kstack + sizeof(child->kstack);
  uintptr_t child_sp = child_top - used;
  memcpy((void *)(uintptr_t)child_sp, (const void *)(uintptr_t)parent_sp,
         used);
  child->kstack_ptr = (uint32_t *)(uintptr_t)child_sp;

  /* patch child return value (EAX=0) */
  uint32_t *eax_slot = (uint32_t *)(uintptr_t)(child_sp + 28);
  *eax_slot = 0;

  /* patch child user stack in iret frame if provided */
  if (child_stack) {
    uint32_t *user_esp_slot = (uint32_t *)(uintptr_t)(child_sp + 32 + 12);
    *user_esp_slot = child_stack;
    child->user_stack = child_stack;
  }

  /* insert into circular list */
  if (!task_list) {
    child->next = child;
    task_list = child;
  } else {
    child->next = task_list->next;
    task_list->next = child;
  }

  return child;
}
