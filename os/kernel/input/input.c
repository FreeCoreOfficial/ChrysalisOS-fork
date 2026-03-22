#include "input.h"
#include "../linux_compat/linux_abi.h"
#include "../time/timer.h"

extern void serial(const char *fmt, ...);

#define INPUT_QUEUE_SIZE 512
#define EVDEV_QUEUE_SIZE 1024

static input_event_t queue[INPUT_QUEUE_SIZE];
static int head = 0;
static int tail = 0;

static struct linux_input_event evdev_queue[EVDEV_QUEUE_SIZE];
static int evdev_head = 0;
static int evdev_tail = 0;

static volatile bool input_ready = false;
static volatile bool usb_keyboard_active = false;

static void evdev_push(uint16_t type, uint16_t code, int32_t value) {
    struct linux_input_event ev;
    uint32_t ms = timer_uptime_ms();
    ev.time.tv_sec = ms / 1000;
    ev.time.tv_usec = (ms % 1000) * 1000;
    ev.type = type;
    ev.code = code;
    ev.value = value;

    int next = (evdev_head + 1) % EVDEV_QUEUE_SIZE;
    if (next != evdev_tail) {
        evdev_queue[evdev_head] = ev;
        evdev_head = next;
    }

    /* Standard evdev practice: send a SYN_REPORT after each physical event */
    if (type != EV_SYN) {
        ev.type = EV_SYN;
        ev.code = SYN_REPORT;
        ev.value = 0;
        next = (evdev_head + 1) % EVDEV_QUEUE_SIZE;
        if (next != evdev_tail) {
            evdev_queue[evdev_head] = ev;
            evdev_head = next;
        }
    }
}

void input_init(void) {
    head = 0;
    tail = 0;
    evdev_head = 0;
    evdev_tail = 0;
    serial("[INPUT] subsystem initialized with evdev support\n");
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

    /* Push to native queue */
    int next = (head + 1) % INPUT_QUEUE_SIZE;
    if (next != tail) {
        queue[head] = event;
        head = next;
    }

    /* Push to evdev queue */
    if (event.type == INPUT_KEYBOARD) {
        evdev_push(EV_KEY, (uint16_t)event.keycode, event.pressed ? 1 : 0);
    } else if (event.type == INPUT_MOUSE_MOVE) {
        evdev_push(EV_REL, REL_X, event.mouse_x);
        evdev_push(EV_REL, REL_Y, event.mouse_y);
    } else if (event.type == INPUT_MOUSE_CLICK) {
        /* Left button code is 0x110 (BTN_LEFT) */
        evdev_push(EV_KEY, 0x110, event.pressed ? 1 : 0);
    }
}

bool input_pop(input_event_t *out_event) {
    if (head == tail) {
        return false;
    }
    
    *out_event = queue[tail];
    tail = (tail + 1) % INPUT_QUEUE_SIZE;
    return true;
}

bool input_pop_evdev(struct linux_input_event *out_event) {
    if (evdev_head == evdev_tail) {
        return false;
    }
    *out_event = evdev_queue[evdev_tail];
    evdev_tail = (evdev_tail + 1) % EVDEV_QUEUE_SIZE;
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
