// ==============================================================================
// SERIAL.C - Pure Hardware I/O Ports Driver
// ==============================================================================
#include <stdint.h>

#define COM1_PORT 0x3F8

static inline void outb(uint16_t port, uint8_t val) {
    __asm__ volatile ("outb %0, %1" : : "a"(val), "Nd"(port));
}

static inline uint8_t inb(uint16_t port) {
    uint8_t ret;
    __asm__ volatile ("inb %1, %0" : "=a"(ret) : "Nd"(port));
    return ret;
}

void init_serial(void) {
    outb(COM1_PORT + 1, 0x00);    // Disable interrupts cleanly during setup
    outb(COM1_PORT + 3, 0x80);    // Assert Line Control Register: Enable DLAB
    outb(COM1_PORT + 0, 0x01);    // Divisor Latch Byte: 115200 Baud Rate target calibration
    outb(COM1_PORT + 1, 0x00);    // High Byte
    outb(COM1_PORT + 3, 0x03);    // Release DLAB, lock parameters: 8 bits, no parity, 1 stop bit
    outb(COM1_PORT + 2, 0xC7);    // Enable FIFO buffer loops, clear active buffers, set 14 bytes trigger
    outb(COM1_PORT + 4, 0x0B);    // Enable auxiliary outputs (RTS + DTR + Interrupt Gateway Out)
}

static inline uint32_t is_transmit_empty(void) {
    return inb(COM1_PORT + 5) & 0x20;
}

void write_serial(char c) {
    while (is_transmit_empty() == 0);
    outb(COM1_PORT, c);
}

void print_serial(const char* str) {
    for (uint32_t i = 0; str[i] != '\0'; i++) {
        if (str[i] == '\n') {
            write_serial('\r'); // Maintain terminal compliance
        }
        write_serial(str[i]);
    }
}