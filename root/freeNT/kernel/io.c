#include "io.h"
#include "types.h"

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
}

void io_clear_screen(void) {
    vga_clear();
}
