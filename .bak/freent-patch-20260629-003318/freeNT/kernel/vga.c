#include "vga.h"

#define VGA_WIDTH 80
#define VGA_HEIGHT 25

static volatile uint16_t* const VGA_MEMORY =
    (uint16_t*)0xB8000;

static uint16_t terminal_row = 0;
static uint16_t terminal_column = 0;

static uint8_t terminal_color =
    (VGA_BLACK << 4) | VGA_WHITE;

static void vga_scroll(void)
{
    if (terminal_row < VGA_HEIGHT)
        return;

    for (uint32_t y = 1; y < VGA_HEIGHT; y++)
    {
        for (uint32_t x = 0; x < VGA_WIDTH; x++)
        {
            VGA_MEMORY[(y - 1) * VGA_WIDTH + x] =
                VGA_MEMORY[y * VGA_WIDTH + x];
        }
    }

    for (uint32_t x = 0; x < VGA_WIDTH; x++)
    {
        VGA_MEMORY[(VGA_HEIGHT - 1) * VGA_WIDTH + x] =
            ((uint16_t)terminal_color << 8) | ' ';
    }

    terminal_row = VGA_HEIGHT - 1;
}

void vga_init(void)
{
    terminal_row = 0;
    terminal_column = 0;

    vga_clear();
}

void vga_clear(void)
{
    for (uint32_t y = 0; y < VGA_HEIGHT; y++)
    {
        for (uint32_t x = 0; x < VGA_WIDTH; x++)
        {
            VGA_MEMORY[y * VGA_WIDTH + x] =
                ((uint16_t)terminal_color << 8) | ' ';
        }
    }

    terminal_row = 0;
    terminal_column = 0;
}

void vga_set_color(
    vga_color_t fg,
    vga_color_t bg)
{
    terminal_color =
        ((uint8_t)bg << 4) |
        (uint8_t)fg;
}

void vga_putc(char c)
{
    if (c == '\n')
    {
        terminal_column = 0;
        terminal_row++;

        vga_scroll();
        return;
    }

    VGA_MEMORY[
        terminal_row * VGA_WIDTH +
        terminal_column
    ] =
        ((uint16_t)terminal_color << 8) |
        (uint8_t)c;

    terminal_column++;

    if (terminal_column >= VGA_WIDTH)
    {
        terminal_column = 0;
        terminal_row++;

        vga_scroll();
    }
}

void vga_write(const char* str)
{
    while (*str)
    {
        vga_putc(*str++);
    }
}

void vga_write_hex(uint64_t value)
{
    static const char hex[] =
        "0123456789ABCDEF";

    vga_write("0x");

    for (int i = 60; i >= 0; i -= 4)
    {
        vga_putc(
            hex[(value >> i) & 0xF]
        );
    }
}

void vga_write_dec(uint64_t value)
{
    char buffer[32];
    int index = 0;

    if (value == 0)
    {
        vga_putc('0');
        return;
    }

    while (value > 0)
    {
        buffer[index++] =
            '0' + (value % 10);

        value /= 10;
    }

    while (index > 0)
    {
        vga_putc(buffer[--index]);
    }
}
