#include "serial.h"

static inline void outb(uint16_t port, uint8_t value)
{
    __asm__ volatile ("outb %0, %1"
                      :
                      : "a"(value), "Nd"(port));
}

static inline uint8_t inb(uint16_t port)
{
    uint8_t ret;

    __asm__ volatile ("inb %1, %0"
                      : "=a"(ret)
                      : "Nd"(port));

    return ret;
}

void serial_init(void)
{
    outb(COM1_PORT + 1, 0x00);
    outb(COM1_PORT + 3, 0x80);

    outb(COM1_PORT + 0, 0x03);
    outb(COM1_PORT + 1, 0x00);

    outb(COM1_PORT + 3, 0x03);
    outb(COM1_PORT + 2, 0xC7);
    outb(COM1_PORT + 4, 0x0B);
}

int serial_received(void)
{
    return inb(COM1_PORT + 5) & 1;
}

char serial_read(void)
{
    while (!serial_received())
    {
    }

    return inb(COM1_PORT);
}

int serial_is_transmit_empty(void)
{
    return inb(COM1_PORT + 5) & 0x20;
}

void serial_write_char(char c)
{
    while (!serial_is_transmit_empty())
    {
    }

    outb(COM1_PORT, (uint8_t)c);
}

void serial_write(const char *str)
{
    while (*str)
    {
        if (*str == '\n')
        {
            serial_write_char('\r');
        }

        serial_write_char(*str++);
    }
}

void serial_write_hex(uint64_t value)
{
    const char hex[] = "0123456789ABCDEF";

    serial_write("0x");

    for (int i = 60; i >= 0; i -= 4)
    {
        serial_write_char(hex[(value >> i) & 0xF]);
    }
}

void serial_write_dec(uint64_t value)
{
    char buffer[32];
    int index = 0;

    if (value == 0)
    {
        serial_write_char('0');
        return;
    }

    while (value > 0)
    {
        buffer[index++] = '0' + (value % 10);
        value /= 10;
    }

    while (index > 0)
    {
        serial_write_char(buffer[--index]);
    }
}