#pragma once
#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    INPUT_KEYBOARD,
    INPUT_MOUSE,
    INPUT_MOUSE_MOVE,
    INPUT_MOUSE_CLICK,
    INPUT_WINDOW_RESIZE
} input_type_t;

typedef struct {
    input_type_t type;
    uint32_t keycode; // ASCII char for simplicity now
    bool pressed;
    int32_t mouse_x;
    int32_t mouse_y;
} input_event_t;

void input_init(void);
void input_push(input_event_t event);
bool input_pop(input_event_t *out_event);
void input_push_key(uint32_t keycode, bool pressed);

/* Synchronization for shell start */
void input_signal_ready(void);
bool input_is_ready(void);

/* USB Keyboard State */
void input_set_usb_keyboard_active(bool active);
bool input_is_usb_keyboard_active(void);

#ifdef __cplusplus
}
#endif
