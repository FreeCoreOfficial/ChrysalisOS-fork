#pragma once
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct user {
  uint32_t uid;
  uint32_t gid;
  char name[32];
  char password[128]; /* plain-text or hash */
  char hostname[32];
  char home[64];
  int is_system; /* 1 = special system user (root-like) */
} user_t;

/* Initialize builtin users (call from kernel_main) */
void user_init(void);

/* Get pointer to current user (read-only) */
user_t *user_get_current(void);

/* Create a user (returns 0 ok, -1 fail) */
int user_create(const char *name, uint32_t uid, const char *password,
                const char *home, int is_system);

/* Switch current user (returns 0 ok, -1 fail) */
int user_switch(const char *name, const char *password);

#ifdef __cplusplus
}
#endif
