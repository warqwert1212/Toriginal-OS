#include "io.h"
#include "types.h"
#include "gfx_terminal.h"
#include "graphics_core.h"

void vga_putc(char c);
void vga_write(const char *str);
void vga_clear(void);
void serial_write_char(char c);
void serial_write(const char *str);

static char  *g_cap_buf = 0;
static size_t g_cap_max = 0;
static size_t g_cap_len = 0;

void io_capture_begin(char *buf, size_t max) {
    g_cap_buf = buf;
    g_cap_max = max;
    g_cap_len = 0;
}

size_t io_capture_end(void) {
    size_t n = g_cap_len;
    if (g_cap_buf && g_cap_max) {
        if (n >= g_cap_max) n = g_cap_max - 1;
        g_cap_buf[n] = '\0';
    } else n = 0;
    g_cap_buf = 0;
    g_cap_max = 0;
    g_cap_len = 0;
    return n;
}

static void capture_put(char c) {
    if (g_cap_len + 1 < g_cap_max) g_cap_buf[g_cap_len++] = c;
}

void io_put_string(const char *s) {
    if (!s) return;
    if (g_cap_buf) { while (*s) capture_put(*s++); return; }
    vga_write(s);
    serial_write(s);   /*this little shit prob wont work*/
}

void io_put_char(char c) {
    if (g_cap_buf) { capture_put(c); return; }
    vga_putc(c);
    serial_write_char(c);
    /* See vga_write()'s comment in vga.c - same reasoning, this is
     * the other entry point (single-char echo while typing) that
     * needs its own flush now that drawing targets a back buffer. */
    if (gterm_is_active()) graphics_present();
}

void io_clear_screen(void) {
    if (g_cap_buf) { capture_put('\f'); return; }
    vga_clear();
}
