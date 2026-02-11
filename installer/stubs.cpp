/* Stubs for Installer to satisfy linker dependencies */
#include <stdarg.h>
#include <stddef.h>
#include <stdint.h>

extern "C" {

/* Basic VGA Text Mode Driver */
static uint16_t *const VGA_MEMORY = (uint16_t *)0xB8000;
static const int VGA_WIDTH = 80;
static const int VGA_HEIGHT = 25;
static int terminal_row = 0;
static int terminal_column = 0;
static uint8_t terminal_color = 0x07; // Light Grey on Black

void terminal_putentryat(char c, uint8_t color, int x, int y) {
  const int index = y * VGA_WIDTH + x;
  VGA_MEMORY[index] = (uint16_t)c | (uint16_t)color << 8;
}

static inline void outb(uint16_t port, uint8_t val) {
  asm volatile("outb %0, %1" : : "a"(val), "Nd"(port));
}

int terminal_get_cursor_x(void) { return terminal_column; }

int terminal_get_cursor_y(void) { return terminal_row; }

void terminal_set_cursor_pos(int x, int y) {
  if (x < 0)
    x = 0;
  if (y < 0)
    y = 0;
  if (x >= VGA_WIDTH)
    x = VGA_WIDTH - 1;
  if (y >= VGA_HEIGHT)
    y = VGA_HEIGHT - 1;

  terminal_column = x;
  terminal_row = y;

  uint16_t pos = y * VGA_WIDTH + x;

  outb(0x3D4, 0x0F);
  outb(0x3D5, (uint8_t)(pos & 0xFF));
  outb(0x3D4, 0x0E);
  outb(0x3D5, (uint8_t)((pos >> 8) & 0xFF));
}

void terminal_scroll() {
  for (int y = 0; y < VGA_HEIGHT - 1; y++) {
    for (int x = 0; x < VGA_WIDTH; x++) {
      const int src_index = (y + 1) * VGA_WIDTH + x;
      const int dst_index = y * VGA_WIDTH + x;
      VGA_MEMORY[dst_index] = VGA_MEMORY[src_index];
    }
  }
  for (int x = 0; x < VGA_WIDTH; x++) {
    const int index = (VGA_HEIGHT - 1) * VGA_WIDTH + x;
    VGA_MEMORY[index] = (uint16_t)' ' | (uint16_t)terminal_color << 8;
  }
}

void terminal_putchar(char c) {
  if (c == '\n') {
    terminal_column = 0;
    if (++terminal_row == VGA_HEIGHT) {
      terminal_row = VGA_HEIGHT - 1;
      terminal_scroll();
    }
  } else {
    terminal_putentryat(c, terminal_color, terminal_column, terminal_row);
    if (++terminal_column == VGA_WIDTH) {
      terminal_column = 0;
      if (++terminal_row == VGA_HEIGHT) {
        terminal_row = VGA_HEIGHT - 1;
        terminal_scroll();
      }
    }
  }
}

void terminal_writestring(const char *s) {
  while (*s)
    terminal_putchar(*s++);
}

void terminal_set_color(uint8_t c) { terminal_color = c; }

void terminal_clear(void) {
  for (int y = 0; y < VGA_HEIGHT; y++) {
    for (int x = 0; x < VGA_WIDTH; x++) {
      terminal_putentryat(' ', terminal_color, x, y);
    }
  }
  terminal_row = 0;
  terminal_column = 0;
}

void terminal_printf(const char *fmt, ...) {
  va_list args;
  va_start(args, fmt);
  while (*fmt) {
    if (*fmt != '%') {
      terminal_putchar(*fmt++);
      continue;
    }
    fmt++;
    // Skip length/width
    while (*fmt == 'l' || *fmt == 'h' || *fmt == 'z' ||
           (*fmt >= '0' && *fmt <= '9') || *fmt == '.')
      fmt++;

    switch (*fmt) {
    case 's': {
      const char *s = va_arg(args, const char *);
      terminal_writestring(s ? s : "(null)");
    } break;
    case 'c':
      terminal_putchar((char)va_arg(args, int));
      break;
    case 'd':
    case 'i': {
      int v = va_arg(args, int);
      if (v < 0) {
        terminal_putchar('-');
        v = -v;
      }
      if (v == 0)
        terminal_putchar('0');
      else {
        char buf[12];
        int i = 0;
        uint32_t val = (uint32_t)v;
        while (val) {
          buf[i++] = '0' + (val % 10);
          val /= 10;
        }
        while (i--)
          terminal_putchar(buf[i]);
      }
    } break;
    case 'u': {
      uint32_t v = va_arg(args, uint32_t);
      if (v == 0)
        terminal_putchar('0');
      else {
        char buf[12];
        int i = 0;
        while (v) {
          buf[i++] = '0' + (v % 10);
          v /= 10;
        }
        while (i--)
          terminal_putchar(buf[i]);
      }
    } break;
    case 'p':
    case 'x': {
      uint32_t v = va_arg(args, uint32_t);
      terminal_writestring("0x");
      for (int i = 28; i >= 0; i -= 4) {
        int d = (v >> i) & 0xF;
        terminal_putchar(d < 10 ? '0' + d : 'a' + d - 10);
      }
    } break;
    case '%':
      terminal_putchar('%');
      break;
    }
    fmt++;
  }
  va_end(args);
}

extern void serial_write(char c);
extern void serial_write_string(const char *s);

void serial(const char *fmt, ...) {
  if (!fmt)
    return;

  va_list args;
  va_start(args, fmt);

  // 1. Output to Serial
  va_list args_serial;
  va_copy(args_serial, args);

  const char *f1 = fmt;
  while (*f1) {
    if (*f1 != '%') {
      serial_write(*f1++);
      continue;
    }
    f1++;
    if (!*f1)
      break;
    while (*f1 == 'l' || *f1 == 'h' || *f1 == 'z' ||
           (*f1 >= '0' && *f1 <= '9') || *f1 == '.')
      f1++;

    switch (*f1) {
    case 's': {
      const char *s = va_arg(args_serial, const char *);
      if (s)
        serial_write_string(s);
      else
        serial_write_string("(null)");
    } break;
    case 'c':
      serial_write((char)va_arg(args_serial, int));
      break;
    case 'd':
    case 'u': {
      int v = va_arg(args_serial, int);
      if (*f1 == 'd' && v < 0) {
        serial_write('-');
        v = -v;
      }
      if (v == 0)
        serial_write('0');
      else {
        char buf[12];
        int i = 0;
        uint32_t val = (uint32_t)v;
        while (val) {
          buf[i++] = '0' + (val % 10);
          val /= 10;
        }
        while (i--)
          serial_write(buf[i]);
      }
    } break;
    case 'p':
    case 'x': {
      uint32_t v = va_arg(args_serial, uint32_t);
      serial_write('0');
      serial_write('x');
      for (int i = 28; i >= 0; i -= 4) {
        int d = (v >> i) & 0xF;
        serial_write(d < 10 ? '0' + d : 'a' + d - 10);
      }
    } break;
    case '%':
      serial_write('%');
      break;
    }
    f1++;
  }
  va_end(args_serial);

  // 2. Output to VGA
  va_list args_vga;
  va_copy(args_vga, args);
  const char *f2 = fmt;
  while (*f2) {
    if (*f2 != '%') {
      terminal_putchar(*f2++);
      continue;
    }
    f2++;
    if (!*f2)
      break;
    while (*f2 == 'l' || *f2 == 'h' || *f2 == 'z' ||
           (*f2 >= '0' && *f2 <= '9') || *f2 == '.')
      f2++;

    switch (*f2) {
    case 's': {
      const char *s = va_arg(args_vga, const char *);
      if (s)
        terminal_writestring(s);
      else
        terminal_writestring("(null)");
    } break;
    case 'c':
      terminal_putchar((char)va_arg(args_vga, int));
      break;
    case 'd':
    case 'u': {
      int v = va_arg(args_vga, int);
      if (*f2 == 'd' && v < 0) {
        terminal_putchar('-');
        v = -v;
      }
      if (v == 0)
        terminal_putchar('0');
      else {
        char buf[12];
        int i = 0;
        uint32_t val = (uint32_t)v;
        while (val) {
          buf[i++] = '0' + (val % 10);
          val /= 10;
        }
        while (i--)
          terminal_putchar(buf[i]);
      }
    } break;
    case 'p':
    case 'x': {
      uint32_t v = va_arg(args_vga, uint32_t);
      terminal_writestring("0x");
      for (int i = 28; i >= 0; i -= 4) {
        int d = (v >> i) & 0xF;
        terminal_putchar(d < 10 ? '0' + d : 'a' + d - 10);
      }
    } break;
    case '%':
      terminal_putchar('%');
      break;
    }
    f2++;
  }
  va_end(args_vga);
  va_end(args);
}
}
