#include "io.h"
#include "string.h"

/* Current cursor position for console output */
static uint16_t cursor_x = 0;
static uint16_t cursor_y = 0;
static const uint16_t SCREEN_WIDTH = 80;
static const uint16_t SCREEN_HEIGHT = 25;
static volatile uint8_t *video_memory = (volatile uint8_t *)0xB8000;

/* Write character to video memory at position */
void io_write_char(uint16_t x, uint16_t y, char c, uint8_t color) {
    if (x >= SCREEN_WIDTH || y >= SCREEN_HEIGHT)
        return;
    
    uint32_t offset = (y * SCREEN_WIDTH + x) * 2;
    video_memory[offset] = (uint8_t)c;
    video_memory[offset + 1] = color;
}

/* Clear the screen */
void io_clear_screen(void) {
    for (uint16_t y = 0; y < SCREEN_HEIGHT; y++) {
        for (uint16_t x = 0; x < SCREEN_WIDTH; x++) {
            io_write_char(x, y, ' ', 0x07);
        }
    }
    cursor_x = 0;
    cursor_y = 0;
}

/* Put a single character */
void io_put_char(char c) {
    if (c == '\n') {
        cursor_y++;
        cursor_x = 0;
        if (cursor_y >= SCREEN_HEIGHT) {
            cursor_y = SCREEN_HEIGHT - 1;
            /* TODO: Scroll screen */
        }
        return;
    }
    
    if (c == '\t') {
        cursor_x += 4;
        return;
    }
    
    io_write_char(cursor_x, cursor_y, c, 0x07);
    cursor_x++;
    
    if (cursor_x >= SCREEN_WIDTH) {
        cursor_x = 0;
        cursor_y++;
        if (cursor_y >= SCREEN_HEIGHT) {
            cursor_y = SCREEN_HEIGHT - 1;
            /* TODO: Scroll screen */
        }
    }
}

/* Put a string */
void io_put_string(const char *str) {
    if (!str)
        return;
    
    while (*str) {
        io_put_char(*str);
        str++;
    }
}

/* Serial port I/O (for debugging) */
#define SERIAL_PORT 0x3F8

void serial_init(void) {
    outb(SERIAL_PORT + 1, 0x00);  /* Disable all interrupts */
    outb(SERIAL_PORT + 3, 0x80);  /* Enable DLAB (set baud rate divisor) */
    outb(SERIAL_PORT + 0, 0x03);  /* Set divisor to 3 (38400 baud) */
    outb(SERIAL_PORT + 1, 0x00);  /* High byte of divisor */
    outb(SERIAL_PORT + 3, 0x03);  /* Disable DLAB, set 8 bits, no parity, 1 stop */
    outb(SERIAL_PORT + 2, 0xC7);  /* Enable FIFO, clear them, with 14-byte threshold */
    outb(SERIAL_PORT + 4, 0x0B);  /* IRQs enabled, RTS/DSR set */
}

void serial_putc(char c) {
    while (!(inb(SERIAL_PORT + 5) & 0x20));  /* Wait for transmit buffer empty */
    outb(SERIAL_PORT, c);
}

char serial_getc(void) {
    while (!(inb(SERIAL_PORT + 5) & 0x01));  /* Wait for receive buffer full */
    return inb(SERIAL_PORT);
}

/* Write a NUL-terminated string to the serial port */
void serial_puts(const char *str) {
    if (!str) return;
    while (*str) {
        serial_putc(*str);
        str++;
    }
}
