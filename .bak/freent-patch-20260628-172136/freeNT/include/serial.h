#ifndef FREENT_SERIAL_H
#define FREENT_SERIAL_H

#include <stdint.h>

#define COM1_PORT 0x3F8

void serial_init(void);

int serial_received(void);
char serial_read(void);

int serial_is_transmit_empty(void);
void serial_write_char(char c);

void serial_write(const char *str);
void serial_write_hex(uint64_t value);
void serial_write_dec(uint64_t value);

/* Convenience aliases (defined in serial.c) — declared here so every
 * caller can `#include "serial.h"` instead of sprinkling ad-hoc
 * `extern void serial_puts(const char *);` declarations everywhere. */
void serial_puts(const char *str);
void serial_putc(char c);
void print_serial(const char *str);

#endif
