#ifndef _KERNEL_IO_H
#define _KERNEL_IO_H

#include "types.h"

/* I/O port operations */
static inline void outb(uint16_t port, uint8_t value) {
    asm volatile("outb %0, %1" : : "a"(value), "Nd"(port));
}

static inline uint8_t inb(uint16_t port) {
    uint8_t value;
    asm volatile("inb %1, %0" : "=a"(value) : "Nd"(port));
    return value;
}

static inline void outw(uint16_t port, uint16_t value) {
    asm volatile("outw %0, %1" : : "a"(value), "Nd"(port));
}

static inline uint16_t inw(uint16_t port) {
    uint16_t value;
    asm volatile("inw %1, %0" : "=a"(value) : "Nd"(port));
    return value;
}

static inline void outl(uint16_t port, uint32_t value) {
    asm volatile("outl %0, %1" : : "a"(value), "Nd"(port));
}

static inline uint32_t inl(uint16_t port) {
    uint32_t value;
    asm volatile("inl %1, %0" : "=a"(value) : "Nd"(port));
    return value;
}

/* Video memory operations */
void io_write_char(uint16_t x, uint16_t y, char c, uint8_t color);
void io_clear_screen(void);
void io_put_char(char c);
void io_put_string(const char *str);

/* Serial port I/O */
void serial_init(void);
void serial_putc(char c);
char serial_getc(void);
void serial_puts(const char *str);

#endif /* _KERNEL_IO_H */
