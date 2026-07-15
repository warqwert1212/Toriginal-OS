#include "panic.h"
#include "vga.h"
#include "serial.h"

void panic(const char* message)
{
    __asm__ volatile("cli");

    vga_set_color(
        VGA_WHITE,
        VGA_RED
    );

    vga_clear();

    vga_write("\n");
    vga_write("=========================================================================\n");
    vga_write("                             KERNEL PANIC!!              \n");
    vga_write("     ohhhh fuck, you messed up bad how the fuck did you kernel panic?       \n");
    vga_write("=============================================================================\n\n");

    vga_write(message);
    vga_write("\n");

    serial_write("\n");
    serial_write("KERNEL PANIC: ");
    serial_write("ok the systems gonna halt now maybe try restarting your system. ");
    serial_write(message);
    serial_write("\n");

    for (;;)
    {
        __asm__ volatile("hlt");
    }
}
