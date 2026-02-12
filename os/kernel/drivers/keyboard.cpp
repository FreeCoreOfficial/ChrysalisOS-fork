// kernel/drivers/keyboard.cpp
#include "keyboard.h"
#include "../arch/i386/io.h"
#include "../cmds/win.h"
#include "../input/input.h"
#include "../interrupts/irq.h"
#include "../time/timer.h"
#include "../video/fb_console.h"
#include "../vt/vt.h"
#include "mouse.h" /* Pentru mouse_init la reload */
#include <stdbool.h>
#include <stdint.h>

/* PS/2 Ports */
#define PS2_DATA 0x60
#define PS2_STATUS 0x64
#define PS2_CMD 0x64

/* Import serial logging from kernel glue */
extern "C" void serial(const char *fmt, ...);

/* Status Register Bits */
#define STATUS_OUTPUT_FULL 0x01
#define STATUS_INPUT_FULL 0x02
#define STATUS_SYSTEM 0x04
#define STATUS_CMD_DATA 0x08 /* 0 = Data, 1 = Command */
#define STATUS_KEYLOCK 0x10
#define STATUS_AUX_FULL 0x20 /* 1 = Mouse data */
#define STATUS_TIMEOUT 0x40
#define STATUS_PARITY 0x80

/* US QWERTY Keymap (embedded for stability) */
static const char keymap_us[128] = {
    0,   27,   '1',  '2', '3',  '4', '5', '6', '7', '8', '9', '0', '-',
    '=', '\b', '\t', 'q', 'w',  'e', 'r', 't', 'y', 'u', 'i', 'o', 'p',
    '[', ']',  '\n', 0,   'a',  's', 'd', 'f', 'g', 'h', 'j', 'k', 'l',
    ';', '\'', '`',  0,   '\\', 'z', 'x', 'c', 'v', 'b', 'n', 'm', ',',
    '.', '/',  0,    '*', 0,    ' ', 0,   0,   0,   0,   0,   0,   0,
    0,   0,    0,    0,   0,    0,   0,   0,   0,   0,   0,   0,   0,
    0,   0,    0,    0,   0,    0,   0,   0,   0};

static const char keymap_us_shift[128] = {
    0,   27,   '!',  '@', '#', '$', '%', '^', '&', '*', '(', ')', '_',
    '+', '\b', '\t', 'Q', 'W', 'E', 'R', 'T', 'Y', 'U', 'I', 'O', 'P',
    '{', '}',  '\n', 0,   'A', 'S', 'D', 'F', 'G', 'H', 'J', 'K', 'L',
    ':', '"',  '~',  0,   '|', 'Z', 'X', 'C', 'V', 'B', 'N', 'M', '<',
    '>', '?',  0,    '*', 0,   ' ', 0,   0,   0,   0,   0,   0,   0,
    0,   0,    0,    0,   0,   0,   0,   0,   0,   0,   0,   0,   0,
    0,   0,    0,    0,   0,   0,   0,   0,   0};

/* state flags */
static bool ctrl_pressed = false;
static bool shift_pressed = false;
static bool alt_pressed = false;
static bool e0_prefix = false;
static bool caps_lock = false;
static uint32_t last_ps2_activity_ms = 0;
static uint32_t last_reset_ms = 0;

static void handle_scancode(uint8_t scancode) {
  /* Handle Extended Scancodes (E0) */
  if (scancode == 0xE0) {
    e0_prefix = true;
    return;
  }

  /* Handle Break Codes (Key Release) */
  if (scancode & 0x80) {
    uint8_t sc = scancode & 0x7F;
    if (sc == 0x2A || sc == 0x36)
      shift_pressed = false;
    else if (sc == 0x1D)
      ctrl_pressed = false;
    else if (sc == 0x38)
      alt_pressed = false;
    e0_prefix = false;
    return;
  }

  /* Handle Make Codes (Key Press) */
  if (scancode == 0x2A || scancode == 0x36) {
    shift_pressed = true;
    return;
  }
  if (scancode == 0x1D) {
    ctrl_pressed = true;
    return;
  }
  if (scancode == 0x38) {
    alt_pressed = true;
    return;
  }
  if (scancode == 0x3A) {
    caps_lock = !caps_lock;
    return;
  }

  /* VT Switching: Ctrl + Alt + F1..F8 */
  if (ctrl_pressed && alt_pressed && scancode >= 0x3B && scancode <= 0x42) {
    int vt_idx = scancode - 0x3B;
    vt_switch(vt_idx);
    return;
  }

  /* Handle Special Keys with E0 prefix */
  if (e0_prefix) {
    e0_prefix = false;
    if (scancode == 0x48) { /* Arrow Up */
      if (!win_is_gui_running())
        fb_cons_scroll(-1);
      input_push_key(0x80, true);
      return;
    } else if (scancode == 0x50) { /* Arrow Down */
      if (!win_is_gui_running())
        fb_cons_scroll(1);
      input_push_key(0x81, true);
      return;
    } else if (scancode == 0x4B) { /* Arrow Left */
      input_push_key(0x82, true);
      return;
    } else if (scancode == 0x4D) { /* Arrow Right */
      input_push_key(0x83, true);
      return;
    } else if (scancode == 0x49) { /* Page Up */
      if (!win_is_gui_running())
        fb_cons_scroll(-10);
      return;
    } else if (scancode == 0x51) { /* Page Down */
      if (!win_is_gui_running())
        fb_cons_scroll(10);
      return;
    }
    return;
  }

  /* Keypad Scroll Fallback */
  if (!win_is_gui_running()) {
    if (scancode == 0x48) {
      fb_cons_scroll(-1);
      return;
    } /* Keypad 8 */
    else if (scancode == 0x50) {
      fb_cons_scroll(1);
      return;
    } /* Keypad 2 */
  }

  /* ASCII Translation */
  char c = 0;
  if (scancode < 128) {
    c = shift_pressed ? keymap_us_shift[scancode] : keymap_us[scancode];
  }

  /* Caps Lock: toggle case for letters */
  if (caps_lock && c) {
    if (c >= 'a' && c <= 'z')
      c -= 32;
    else if (c >= 'A' && c <= 'Z')
      c += 32;
  }

  /* Apply Control Modifier (Ctrl+A..Z -> 1..26) */
  if (ctrl_pressed && c) {
    if (c >= 'a' && c <= 'z')
      c -= 96;
    else if (c >= 'A' && c <= 'Z')
      c -= 64;
  }

  if (c) {
    input_push_key((uint32_t)c, true);
  }
}

extern "C" void ps2_update_activity(void) {
  last_ps2_activity_ms = timer_uptime_ms();
}

/* Helpers pentru sincronizare PS/2 */
static void kbd_wait_write() {
  int timeout = 100000;
  while (timeout--) {
    if ((inb(PS2_STATUS) & STATUS_INPUT_FULL) == 0)
      return;
    asm volatile("pause");
  }
}

static void kbd_wait_read() {
  int timeout = 100000;
  while (timeout--) {
    if ((inb(PS2_STATUS) & STATUS_OUTPUT_FULL) == 1)
      return;
    asm volatile("pause");
  }
}

extern "C" void keyboard_handler(registers_t *regs) {
  (void)regs;
  ps2_update_activity();

  /* Safety limit to prevent infinite loops in ISR */
  int loops = 0;
  const int max_loops = 100;

  /* Loop to handle all pending data (robustness against IRQ sharing/latency) */
  while (loops < max_loops) {
    uint8_t status = inb(PS2_STATUS);

    /* If output buffer is empty, we are done */
    if (!(status & STATUS_OUTPUT_FULL)) {
      break;
    }

    /* If data is from Mouse (Aux) but triggered IRQ1, we MUST read it to clear
       the controller, otherwise the system freezes waiting for a read that
       never happens. We discard it to keep the keyboard alive. */
    if (status & STATUS_AUX_FULL) {
      /* Leave for IRQ12 handler to handle properly. */
      break;
    }

    /* Read Scancode */
    uint8_t scancode = inb(PS2_DATA);
    last_ps2_activity_ms = timer_uptime_ms();

    /* Allow PS/2 even if USB is active to avoid total input loss */

    handle_scancode(scancode);
    loops++;
  }
}

extern "C" void keyboard_init() {
  serial("[PS/2] Initializing keyboard...\n");
  serial("[PS/2] Device init start (IRQ1)\n");

  /* 1. Disable Devices */
  kbd_wait_write();
  outb(PS2_CMD, 0xAD); // Disable Keyboard
  kbd_wait_write();
  outb(PS2_CMD, 0xA7); // Disable Mouse

  /* 2. Flush Output Buffer */
  while (inb(PS2_STATUS) & STATUS_OUTPUT_FULL)
    inb(PS2_DATA);

  /* 3. Set Configuration Byte */
  kbd_wait_write();
  outb(PS2_CMD, 0x20); // Read Config
  kbd_wait_read();
  uint8_t config = inb(PS2_DATA);

  config |= 0x01; // Enable IRQ1 (Keyboard)
  config |= 0x02; // Enable IRQ12 (Mouse) - Critical for mouse interrupts!
  config |= 0x40; // Enable Translation (Set 2 -> Set 1)

  kbd_wait_write();
  outb(PS2_CMD, 0x60); // Write Config
  kbd_wait_write();
  outb(PS2_DATA, config);

  /* 4. Enable Keyboard */
  kbd_wait_write();
  outb(PS2_CMD, 0xAE);

  /* 5. Reset Keyboard */
  kbd_wait_write();
  outb(PS2_DATA, 0xFF);
  kbd_wait_read();
  inb(PS2_DATA); // ACK
  kbd_wait_read();
  inb(PS2_DATA); // BAT

  /* 6. Enable Scanning (Explicit) - Fixes dead keyboard after reboot */
  kbd_wait_write();
  outb(PS2_DATA, 0xF4);
  kbd_wait_read();
  inb(PS2_DATA); // ACK

  /* 7. Install Handler */
  irq_install_handler(1, keyboard_handler);

  /* 8. Unmask IRQ1 in PIC (Master) */
  uint8_t mask = inb(0x21);
  outb(0x21, mask & 0xFD);

  serial("[PS/2] Keyboard initialized.\n");
}

/* --- Watchdog Implementation --- */
static int watchdog_stuck_count = 0;

void ps2_controller_watchdog(void) {
  uint8_t status = inb(PS2_STATUS);
  uint32_t now = timer_uptime_ms();

  /* Verificăm dacă există date în buffer (Bit 0 = 1) */
  if (status & STATUS_OUTPUT_FULL) {
    watchdog_stuck_count++;

    /* Dacă datele stau necitite prea mult timp (aprox 2-3 secunde la 100Hz),
       înseamnă că întreruperile nu s-au declanșat sau controllerul e blocat. */
    if (watchdog_stuck_count > 200) {
      serial(
          "[PS/2] Watchdog: Stuck data detected (Status=0x%x). Flushing...\n",
          status);

      /* Citim forțat pentru a debloca linia */
      uint8_t garbage = inb(PS2_DATA);
      (void)garbage;

      /* Dacă problema persistă (aprox 5 secunde), dăm reload complet */
      if (watchdog_stuck_count > 500) {
        serial("[PS/2] Watchdog: Devices unresponsive. Reloading drivers...\n");
        keyboard_init();
        mouse_init();
        watchdog_stuck_count = 0;
      }
    }
  } else {
    /* Buffer gol, totul e OK */
    watchdog_stuck_count = 0;
  }

  /* REMOVED: redundant poll keyboard logic that caused duplicate characters.
     We rely on IRQ1 (keyboard_handler) for processing scancodes.
     The watchdog only focuses on de-blocking stuck buffers as handled above. */

  /* If output buffer is full but no IRQ fired for a long time, we might need a
     reset handled above in the watchdog_stuck_count logic.
     The 'No scancodes' re-init was too aggressive and killed the mouse.
     We removed it to ensure stability while only using the mouse. */
}
