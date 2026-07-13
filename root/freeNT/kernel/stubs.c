
#include <stdint.h>
#include "io.h"
#include "keyboard.h"
#include "serial.h"

void main_oobe_setup(void)
{
    io_clear_screen();
    io_put_string("==================================================\n");
    io_put_string("        this is the stubs                   \n");
    io_put_string("==================================================\n\n");
    io_put_string("  Kernel: freeNT  |  Made by warqwert\n\n");
    io_put_string("  Type 'help' for available commands.\n\n");

    serial_puts("[OOBE] Dropping into OS shell.\n");

    extern void kernel_os_shell(void);
    kernel_os_shell();
}
