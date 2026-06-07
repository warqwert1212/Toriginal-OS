#include <stdint.h>
#include "serial.h"

static volatile uint16_t* const VGA =
    (volatile uint16_t*)0xB8000;

static uint16_t cursor_x = 0;
static uint16_t cursor_y = 0;

static void vga_putc(char c)
{
    if (c == '\n')
    {
        cursor_x = 0;
        cursor_y++;
        return;
    }

    VGA[cursor_y * 80 + cursor_x] =
        (0x0F << 8) | c;

    cursor_x++;

    if (cursor_x >= 80)
    {
        cursor_x = 0;
        cursor_y++;
    }
}

static void vga_write(const char* str)
{
    while (*str)
    {
        vga_putc(*str++);
    }
}

void kernel_main(uint32_t multiboot_magic,
                 uint32_t multiboot_info)
{
    (void)multiboot_magic;
    (void)multiboot_info;

    serial_init();

    serial_write("\n");
    serial_write("====================================\n");
    serial_write("      FreeNT Kernel Starting\n");
    serial_write("====================================\n");

    vga_write("FreeNT Kernel Booted\n");

    serial_write("Serial initialized\n");
    serial_write("Kernel entered successfully\n");

    for (;;)
    {
        __asm__ volatile ("hlt");
    }
}