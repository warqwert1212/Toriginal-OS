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

static const char serial_hex_digits[] = "0123456789ABCDEF";

void serial_write_hex(uint64_t value)
{
    char buf[17];
    for (int i = 0; i < 16; i++)
        buf[i] = serial_hex_digits[(value >> (60 - i*4)) & 0xF];
    buf[16] = '\0';
    serial_write(buf);
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

/* Convenience aliases */
void serial_puts(const char *str)
{
    serial_write(str);
}

void serial_putc(char c)
{
    serial_write_char(c);
}

void print_serial(const char *str)
{
    serial_write(str);
}
