// ==============================================================================
// STUBS.C
// Provides stub implementations for symbols that aren't built yet in 1.0.
// ==============================================================================

#include <stdint.h>
#include "io.h"
#include "keybord.h"

// ---------------------------------------------------------------------------
// main_oobe_setup
// Previously a silent empty stub — now drops straight into the OS shell
// so boot actually lands somewhere useful.
// ---------------------------------------------------------------------------
void main_oobe_setup(void) {
    // Print welcome banner to both VGA and serial
    io_clear_screen();

    io_put_string("==================================================\n");
    io_put_string("        Welcome to Toriginal OS v1.0             \n");
    io_put_string("==================================================\n");
    io_put_string("\n");
    io_put_string("  Kernel: freeNT  |  Made by warqwert\n");
    io_put_string("\n");
    io_put_string("  Type 'help' for available commands.\n");
    io_put_string("\n");

    serial_puts("[OOBE] Dropping into OS shell.\n");

    // Hand off to the real shell
    // kernel_os_shell is defined in shell/shell.c
    extern void kernel_os_shell(void);
    kernel_os_shell();
}

// ---------------------------------------------------------------------------
// Embedded shell binary symbols
//
// FIXED: These were declared as uint8_t[] which broke install.c's size
// arithmetic. The linker symbol _binary_..._size is meant to be used as:
//     size_t sz = (size_t)&_binary_toriginal_shell_bin_size;
// So it must be declared as a single extern symbol whose ADDRESS is the size,
// not an array. We provide a stub value of 1 so the NULL/zero guard in
// install.c passes without crashing.
// ---------------------------------------------------------------------------
uint8_t  _binary_toriginal_shell_bin_start[1] = { 0 };

// This is deliberately an integer, not an array.
// install.c casts &_binary_toriginal_shell_bin_size to uint64_t to get the size.
// Stub value 1 just prevents the "payload reporting 0 bytes" error path.
uint8_t  _binary_toriginal_shell_bin_size = 1;