#pragma once

/* Wrapper compatibil: redirecționează include-ul la header-ul canonical */
#include "../task.h"

/* NOTĂ: nu punem extern "C" aici! header-ul canonical
   (kernel/include/task/task.h) conține deja prototipurile C cu extern "C". Aici
   definim numai helper-e C++ (overload-uri/aliasuri) — cu linkage C++ — ca să
   nu intre în conflict. */

#ifdef __cplusplus

/* vechea task_create(name, entry) -> apelează noua funcție task_create(entry,
 * pid=0) */
static inline task_t *task_create(const char * /*name*/, void (*entry)(void)) {
  return task_create(entry, 0);
}

/* alias vechi pentru yield */
static inline void task_yield(void) { yield(); }

/* alias vechi pentru init */
static inline void task_init(void) { task_init_scheduler(); }

#endif /* __cplusplus */
