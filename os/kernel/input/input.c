#include "input.h"

/* Kernel logging helper */
extern void serial(const char *fmt, ...);

#define INPUT_QUEUE_SIZE 512

static input_event_t queue[INPUT_QUEUE_SIZE];
static int head = 0;
static int tail = 0;
static volatile bool input_ready = false;
static volatile bool usb_keyboard_active = false;

void input_init(void) {
    head = 0;
    tail = 0;
    serial("[INPUT] subsystem initialized\n");
}

void input_push(input_event_t event) {
    /* Coalesce consecutive mouse-move events */
    if (event.type == INPUT_MOUSE_MOVE && head != tail) {
        int last = (head - 1 + INPUT_QUEUE_SIZE) % INPUT_QUEUE_SIZE;
        if (queue[last].type == INPUT_MOUSE_MOVE) {
            queue[last] = event;
            return;
        }
    }

    int next = (head + 1) % INPUT_QUEUE_SIZE;
    if (next == tail) {
        /* Queue full: drop mouse-move, otherwise try to drop oldest mouse-move */
        if (event.type == INPUT_MOUSE_MOVE) {
            return;
        }
        if (queue[tail].type == INPUT_MOUSE_MOVE) {
            tail = (tail + 1) % INPUT_QUEUE_SIZE;
        } else {
            /* Full of non-mouse events, drop this event */
            return;
        }
        next = (head + 1) % INPUT_QUEUE_SIZE;
        if (next == tail) {
            return;
        }
    }

    queue[head] = event;
    head = next;
}

bool input_pop(input_event_t *out_event) {
    if (head == tail) {
        return false;
    }
    
    *out_event = queue[tail];
    tail = (tail + 1) % INPUT_QUEUE_SIZE;
    return true;
}

void input_push_key(uint32_t keycode, bool pressed) {
    input_event_t ev = {0};
    ev.type = INPUT_KEYBOARD;
    ev.keycode = keycode;
    ev.pressed = pressed;
    input_push(ev);
}

void input_signal_ready(void) {
    input_ready = true;
}

bool input_is_ready(void) {
    return input_ready;
}

void input_set_usb_keyboard_active(bool active) {
    usb_keyboard_active = active;
}

bool input_is_usb_keyboard_active(void) {
    return usb_keyboard_active;
}
