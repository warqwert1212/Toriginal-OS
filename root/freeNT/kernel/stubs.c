/* stubs.c — high-level kernel stubs.
 *
 * FIX: _binary_toriginal_shell_bin_start/size moved to runtime_stubs.c
 *      to avoid duplicate-symbol errors.
 * FIX: keybord.h -> keyboard.h (typo).
 *
 * NOTE: main_oobe_setup() here is currently unused (nothing calls it —
 * kernel_main() goes straight to kernel_os_shell()). A much richer OOBE
 * implementation exists as a design reference at
 * root/sys/userpc/home/useraccount1/apps/oobe.c, but it depends on GUI
 * hooks (sys_shell_print, sys_execute_program, auth_update_username,
 * a user_config_t global, etc.) that don't exist in this kernel tree
 * yet, so it is intentionally NOT compiled into the kernel — including
 * it would either fail to link (undefined references) or, if those
 * symbols were stubbed out, collide with this function's name. Once the
 * GUI/auth subsystem is built, that file is the place to wire a real
 * OOBE flow in; until then this trivial version is the one in the build.
 */

#include <stdint.h>
#include "io.h"
#include "keyboard.h"
#include "serial.h"

void main_oobe_setup(void)
{
    io_clear_screen();
    io_put_string("==================================================\n");
    io_put_string("        Welcome to Toriginal OS v1.0             \n");
    io_put_string("==================================================\n\n");
    io_put_string("  Kernel: freeNT  |  Made by warqwert\n\n");
    io_put_string("  Type 'help' for available commands.\n\n");

    serial_puts("[OOBE] Dropping into OS shell.\n");

    extern void kernel_os_shell(void);
    kernel_os_shell();
}
