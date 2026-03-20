#include "usb_hid.h"
#include "usb_core.h"
#include "../drivers/serial.h"
#include "../mem/kmalloc.h"
#include "../string.h"
#include "../input/input.h"
#include "../time/timer.h"

#define MAX_HID_DEVICES 4

typedef struct {
    int active;
    uint8_t addr;
    uint8_t ep;
    void* td_handle;
    uint8_t* buffer;
    uint8_t prev_buffer[8];
    int has_input;
    uint32_t last_report_ms;
} hid_device_t;

static hid_device_t hid_devices[MAX_HID_DEVICES];

/* Simple HID to ASCII Map (US QWERTY) */
static const char hid_ascii_map[128] = {
    0, 0, 0, 0, 'a', 'b', 'c', 'd', 'e', 'f', 'g', 'h', 'i', 'j', 'k', 'l',
    'm', 'n', 'o', 'p', 'q', 'r', 's', 't', 'u', 'v', 'w', 'x', 'y', 'z',
    '1', '2', '3', '4', '5', '6', '7', '8', '9', '0', '\n', 0, '\b', '\t',
    ' ', '-', '=', '[', ']', '\\', 0, ';', '\'', '`', ',', '.', '/', 0, 0, 0
};

void usb_hid_init(uint8_t addr, uint8_t* config_desc, uint16_t config_len) {
    serial_write_string("[USB HID] Initializing device...\r\n");
    serial_printf("[USB HID] Device init start (addr=%d)\n", addr);

    uint8_t* ptr = config_desc;
    uint8_t* end = config_desc + config_len;
    uint8_t hid_ep = 0;
    uint16_t hid_ep_size = 8;

    while (ptr < end) {
        uint8_t len = ptr[0];
        uint8_t type = ptr[1];
        if (len == 0) break;

        if (type == USB_DESC_INTERFACE) {
            if (ptr[5] == 0x03 && ptr[6] == 0x01 && ptr[7] == 0x01) {
                serial_write_string("[USB HID] Keyboard interface found\r\n");
            }
        } else if (type == USB_DESC_ENDPOINT) {
            if ((ptr[2] & 0x80) && (ptr[3] & 0x03) == 0x03) {
                hid_ep = ptr[2] & 0x0F;
                hid_ep_size = *(uint16_t*)(ptr + 4);
                serial_printf("[USB HID] Interrupt IN endpoint found: 0x8%x, size: %d\n", hid_ep, hid_ep_size);
                break;
            }
        }
        ptr += len;
    }

    if (hid_ep) {
        for (int i = 0; i < MAX_HID_DEVICES; i++) {
            if (!hid_devices[i].active) {
                hid_devices[i].active = 1;
                hid_devices[i].addr = addr;
                hid_devices[i].ep = hid_ep;
                hid_devices[i].buffer = (uint8_t*)kmalloc(hid_ep_size);
                hid_devices[i].td_handle = usb_interrupt_setup(addr, hid_ep, hid_devices[i].buffer, hid_ep_size);
                memset(hid_devices[i].prev_buffer, 0, 8);
                hid_devices[i].has_input = 0;
                hid_devices[i].last_report_ms = 0;
                serial_write_string("[USB HID] Polling started for new device.\r\n");
                serial_printf("[USB HID] Device init done (addr=%d, ep=%d)\n", addr, hid_ep);
                return;
            }
        }
    }
}

void usb_hid_poll(void) {
    uint32_t now_ms = timer_uptime_ms();
    int any_recent = 0;
    for (int i = 0; i < MAX_HID_DEVICES; i++) {
        if (!hid_devices[i].active) continue;

        hid_device_t* dev = &hid_devices[i];

        if (usb_interrupt_poll(dev->td_handle)) {
            dev->last_report_ms = now_ms;
            if (!dev->has_input) {
                dev->has_input = 1;
                input_set_usb_keyboard_active(true);
            }
            /* Log raw report */
            // serial_printf("[USB HID] report: %02x %02x %02x %02x %02x %02x %02x %02x\n",
            //        dev->buffer[0], dev->buffer[1], dev->buffer[2], dev->buffer[3],
            //        dev->buffer[4], dev->buffer[5], dev->buffer[6], dev->buffer[7]);

            uint8_t modifiers = dev->buffer[0];
            int shift = (modifiers & 0x02) || (modifiers & 0x20);

            /* Check for Releases (keys in prev but not in curr) */
            for (int j = 2; j < 8; j++) {
                uint8_t old_key = dev->prev_buffer[j];
                if (old_key > 0) {
                    int still_pressed = 0;
                    for (int k = 2; k < 8; k++) {
                        if (dev->buffer[k] == old_key) {
                            still_pressed = 1;
                            break;
                        }
                    }
                    if (!still_pressed) {
                        /* RELEASE EVENT */
                        if (old_key < 128) {
                            char c = hid_ascii_map[old_key];
                            if (shift && c >= 'a' && c <= 'z') c -= 32;
                            if (c) input_push_key((uint32_t)c, false);
                        }
                    }
                }
            }

            /* Check for Presses (keys in curr but not in prev) */
            for (int k = 2; k < 8; k++) {
                uint8_t key = dev->buffer[k];
                if (key > 0) {
                    int new_press = 1;
                    for (int j = 2; j < 8; j++) {
                        if (dev->prev_buffer[j] == key) {
                            new_press = 0;
                            break;
                        }
                    }

                    if (new_press) {
                        /* PRESS EVENT */
                        if (key < 128) {
                            char c = hid_ascii_map[key];
                            if (shift && c >= 'a' && c <= 'z') {
                                c -= 32;
                            }
                            if (c) {
                                // serial_printf("[USB HID] Decoded: %c (0x%x)\n", c, c);
                                input_push_key((uint32_t)c, true);
                            }
                        }
                    }
                }
            }
            memcpy(dev->prev_buffer, dev->buffer, 8);
        }

        if (dev->last_report_ms != 0 && (now_ms - dev->last_report_ms) < 3000) {
            any_recent = 1;
        }
    }

    /* If no HID input for a while, allow PS/2 to take over */
    if (!any_recent) {
        input_set_usb_keyboard_active(false);
    }
}
