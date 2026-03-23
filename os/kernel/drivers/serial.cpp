#include "serial.h"
#include "../arch/i386/io.h"
#include <stddef.h>
#include <stdint.h>
#include <stdarg.h>

#ifdef __x86_64__
#include "../sched/task64.h"
#endif

#define COM1 0x3F8

void serial_init()
{
    outb(COM1 + 1, 0x00);    // Disable interrupts
    outb(COM1 + 3, 0x80);    // Enable DLAB
    outb(COM1 + 0, 0x03);    // Baud rate divisor (lo byte) = 3 → 38400
    outb(COM1 + 1, 0x00);    // hi byte
    outb(COM1 + 3, 0x03);    // 8 bits, no parity, one stop bit
    outb(COM1 + 2, 0xC7);    // Enable FIFO, clear, 14-byte threshold
    outb(COM1 + 4, 0x0B);    // IRQs enabled, RTS/DSR set
}

int serial_received()
{
    return inb(COM1 + 5) & 1;
}

char serial_read()
{
    while (!serial_received()) {
#ifdef __x86_64__
        task64_yield();
#endif
    }
    return inb(COM1);
}

int serial_is_transmit_empty()
{
    return inb(COM1 + 5) & 0x20;
}

void serial_write(char c)
{
    while (!serial_is_transmit_empty()) {
#ifdef __x86_64__
        task64_yield();
#endif
    }
    outb(COM1, c);
}

void serial_write_string(const char* str)
{
    while (*str)
        serial_write(*str++);
}

static void serial_write_uint(uint32_t v)
{
    char buf[11];
    int i = 0;

    if (v == 0) {
        serial_write('0');
        return;
    }

    while (v) {
        buf[i++] = '0' + (v % 10);
        v /= 10;
    }

    while (i--)
        serial_write(buf[i]);
}

static void serial_write_int(int32_t v)
{
    if (v < 0) {
        serial_write('-');
        v = -v;
    }
    serial_write_uint((uint32_t)v);
}

extern "C" void serial_write_hex(uint64_t v)
{
    serial_write_string("0x");
#if defined(__x86_64__)
    for (int i = 60; i >= 0; i -= 4) {
        uint8_t d = (v >> i) & 0xF;
        serial_write(d < 10 ? '0' + d : 'a' + d - 10);
    }
#else
    for (int i = 28; i >= 0; i -= 4) {
        uint8_t d = (uint8_t)((v >> i) & 0xF);
        serial_write(d < 10 ? '0' + d : 'a' + d - 10);
    }
#endif
}

extern "C" void serial_vprintf(const char* fmt, va_list args)
{
    while (*fmt) {
        if (*fmt != '%') {
            serial_write(*fmt++);
            continue;
        }

        fmt++; // skip %

        switch (*fmt) {
        case 's':
            serial_write_string(va_arg(args, const char*));
            break;
        case 'c':
            serial_write((char)va_arg(args, int));
            break;
        case 'd':
            serial_write_int(va_arg(args, int));
            break;
        case 'u':
            serial_write_uint(va_arg(args, uint32_t));
            break;
        case 'x':
            serial_write_hex(va_arg(args, uint64_t));
            break;
        case 'p': {
            uintptr_t v = (uintptr_t)va_arg(args, void*);
            serial_write_hex((uint64_t)v);
            break;
        }
        case '%':
            serial_write('%');
            break;
        default:
            serial_write('%');
            serial_write(*fmt);
            break;
        }
        fmt++;
    }
}

extern "C" void serial_printf(const char* fmt, ...)
{
    va_list args;
    va_start(args, fmt);
    serial_vprintf(fmt, args);
    va_end(args);
}

#ifdef __x86_64__
extern "C" void serial(const char* fmt, ...)
{
    va_list args;
    va_start(args, fmt);
    serial_vprintf(fmt, args);
    va_end(args);
}
#endif
