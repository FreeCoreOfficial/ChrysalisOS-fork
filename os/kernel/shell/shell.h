#pragma once
#include "../input/input.h"

typedef struct window window_t;

#ifdef __cplusplus
extern "C" {
#endif

void shell_init();
void shell_handle_char(char c);

/* API pentru shortcuts */
void shell_reset_input(void);
void shell_prompt(void);

/* new: pollează inputul din buffer (apelat din loop-ul principal) */
void shell_poll_input(void);

/* Multi-instance support */
void shell_init_context(int id);
void shell_set_active_context(int id);

/* GUI Integration */
void shell_create_window(void);
void shell_destroy_window(void);
int  shell_is_window_active(void);

/* Event handling for window mode */
bool shell_handle_event(input_event_t* ev);
window_t* shell_get_window(void);

#ifdef __cplusplus
}
#endif
