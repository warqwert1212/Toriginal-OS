/* stubs.c — high-level kernel stubs.
 *
 * FIX: _binary_toriginal_shell_bin_start/size moved to runtime_stubs.c
 *      to avoid duplicate-symbol errors.
 */

#include <stdint.h>
#include "io.h"
#include "keybord.h"

void main_oobe_setup(void)
{
    io_clear_screen();
    io_put_string("==================================================\n");
    io_put_string("        Welcome to Toriginal OS v1.0             \n");
    io_put_string("==================================================\n\n");
    io_put_string("  Kernel: freeNT  |  Made by warqwert\n\n");
    io_put_string("  Type 'help' for available commands.\n\n");

    extern void serial_puts(const char *);
    serial_puts("[OOBE] Dropping into OS shell.\n");

    extern void kernel_os_shell(void);
    kernel_os_shell();
}