#include "../events/event_queue.h"
#include "../string.h"
#include "../terminal.h"
#include "../user/user.h"

#ifdef __cplusplus
extern "C" {
#endif

extern "C" void yield(void);

static void shell_read_line(char *buf, int max, bool mask) {
  int pos = 0;
  while (pos < max - 1) {
    event_t ev;
    if (event_pop(&ev) == 0) {
      if (ev.type == EVENT_KEY && ev.key.pressed && ev.key.ascii) {
        char c = ev.key.ascii;
        if (c == '\n' || c == '\r') {
          terminal_putchar('\n');
          break;
        } else if (c == '\b') {
          if (pos > 0) {
            pos--;
            terminal_putchar('\b');
            terminal_putchar(' ');
            terminal_putchar('\b');
          }
        } else if (c >= 32 && c <= 126) {
          buf[pos++] = c;
          terminal_putchar(mask ? '*' : c);
        }
      }
    }
    yield();
  }
  buf[pos] = 0;
}

/* usage: login <username> [password] */
int cmd_login_main(int argc, char **argv) {
  if (argc < 2) {
    terminal_printf("Usage: login <username> [password] [DEPRECATED]\n");
    return -1;
  }
  const char *user = argv[1];
  const char *pass = (argc >= 3) ? argv[2] : "";
  int r = user_switch(user, pass);
  if (r == 0)
    return 0;
  terminal_printf("login: failed for '%s'\n", user);
  return -1;
}

/* usage: add-user */
void cmd_add_user(int argc, char **argv) {
  (void)argc;
  (void)argv;

  char name[32];
  char pass[128];

  terminal_printf("New Username: ");
  shell_read_line(name, 32, false);

  if (strlen(name) == 0) {
    terminal_printf("Error: Username cannot be empty.\n");
    return;
  }

  terminal_printf("New Password: ");
  shell_read_line(pass, 128, true);

  /* Using 1000+ for normal users, root is 0 */
  /* Home is /home/name */
  char home[64] = "/home/";
  strcat(home, name);

  int r = user_create(name, 1001, pass, home, 0);
  if (r == 0) {
    terminal_printf("User '%s' created successfully.\n", name);
  } else {
    terminal_printf("Error: Failed to create user '%s'.\n", name);
  }
}

#ifdef __cplusplus
}
#endif
