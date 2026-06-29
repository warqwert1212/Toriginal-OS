/* =============================================================================
 * io.c — Kernel I/O layer
 *
 * Thin wrappers that fan output to both VGA and serial so the rest of the
 * kernel only needs to call io_put_string() / io_put_char().
 *
 * NOTE: runtime_stubs.c previously provided these as serial-only stubs.
 * This file replaces those stubs with proper dual-output implementations.
 * runtime_stubs.c no longer defines io_put_string etc.
 * ========================================================================== */

#include "io.h"
#include "types.h"

/* Forward declarations — implementations live in vga.c and serial.c */
void vga_putc(char c);
void vga_write(const char *str);
void vga_clear(void);
void serial_write_char(char c);
void serial_write(const char *str);

void io_put_string(const char *s) {
    if (!s) return;
    vga_write(s);
    serial_write(s);
}

void io_put_char(char c) {
    vga_putc(c);
    serial_write_char(c);
}

void io_clear_screen(void) {
    vga_clear();
}

void io_put_hex(uint64_t v) {
    /* Simple hex dump to serial only (VGA has limited width) */
    const char hex[] = "0123456789ABCDEF";
    serial_write("0x");
    for (int i = 60; i >= 0; i -= 4)
        serial_write_char(hex[(v >> i) & 0xF]);
}
