#pragma once
#include <stdint.h>
#include <stdarg.h>

#ifdef __cplusplus
extern "C" {
#endif

void serial_init();
int  serial_received();
char serial_read();
int  serial_is_transmit_empty();
void serial_write(char c);
void serial_write_string(const char* str);

/* NEW */
void serial_printf(const char* fmt, ...);
void serial_write_hex(uint64_t val);

#ifdef __cplusplus
}
#endif
