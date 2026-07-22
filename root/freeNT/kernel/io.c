#include "io.h"
#include "types.h"
#include "gfx_terminal.h"
#include "graphics_core.h"

void vga_putc(char c);
void vga_write(const char *str);
void vga_clear(void);
void serial_write_char(char c);
void serial_write(const char *str);

void io_put_string(const char *s) {
    if (!s) return;
    vga_write(s);
    serial_write(s);   /*this little shit prob wont work*/
}

void io_put_char(char c) {
    vga_putc(c);
    serial_write_char(c);
    /* See vga_write()'s comment in vga.c - same reasoning, this is
     * the other entry point (single-char echo while typing) that
     * needs its own flush now that drawing targets a back buffer. */
    if (gterm_is_active()) graphics_present();
}

void io_clear_screen(void) {
    vga_clear();
}
