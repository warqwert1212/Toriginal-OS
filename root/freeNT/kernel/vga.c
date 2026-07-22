/* =============================================================================
 * vga.c — VGA text-mode (0xB8000) driver
 * ============================================================================= */

#include "vga.h"
#include "gfx_terminal.h"
#include "graphics_core.h"

#define VGA_WIDTH   80
#define VGA_HEIGHT  25

#define VGA_CRTC_INDEX 0x3D4
#define VGA_CRTC_DATA  0x3D5

static volatile uint16_t* const VGA_MEMORY = (uint16_t*)0xB8000;

static uint16_t terminal_row    = 0;
static uint16_t terminal_column = 0;

static uint8_t terminal_color = (uint8_t)((VGA_BLACK << 4) | VGA_WHITE);

static int g_statusbar_enabled = 0;

static inline uint16_t top_row(void)
{
    return g_statusbar_enabled ? 1 : 0;
}

static inline uint16_t vga_entry(char c)
{
    return ((uint16_t)terminal_color << 8) | (uint8_t)c;
}

static inline void vga_outb(uint16_t port, uint8_t val)
{
    __asm__ volatile("outb %0, %1" :: "a"(val), "Nd"(port));
}

static inline uint8_t vga_inb(uint16_t port)
{
    uint8_t v;
    __asm__ volatile("inb %1, %0" : "=a"(v) : "Nd"(port));
    return v;
}

void vga_set_cursor(uint16_t row, uint16_t col)
{
    if (gterm_is_active()) {
        /* gterm_set_cursor() clamps against its own grid dimensions
         * internally (which differ from VGA's fixed 80x25 - gterm is
         * 64x24 at the locked 1024x768/16x32-cell geometry), so no
         * clamping is done here - passing raw row/col through and
         * letting gterm own its own bounds-checking keeps this
         * function from needing to know gterm's grid size at all. */
        gterm_set_cursor(row, col);
        return;
    }

    if (row >= VGA_HEIGHT) row = VGA_HEIGHT - 1;
    if (col >= VGA_WIDTH)  col = VGA_WIDTH - 1;

    uint16_t pos = (uint16_t)(row * VGA_WIDTH + col);

    vga_outb(VGA_CRTC_INDEX, 0x0F);
    vga_outb(VGA_CRTC_DATA, (uint8_t)(pos & 0xFF));
    vga_outb(VGA_CRTC_INDEX, 0x0E);
    vga_outb(VGA_CRTC_DATA, (uint8_t)((pos >> 8) & 0xFF));
}

void vga_enable_cursor(uint8_t cursor_start, uint8_t cursor_end)
{
    vga_outb(VGA_CRTC_INDEX, 0x0A);
    vga_outb(VGA_CRTC_DATA, (uint8_t)((vga_inb(VGA_CRTC_DATA) & 0xC0) | (cursor_start & 0x1F)));

    vga_outb(VGA_CRTC_INDEX, 0x0B);
    vga_outb(VGA_CRTC_DATA, (uint8_t)((vga_inb(VGA_CRTC_DATA) & 0xE0) | (cursor_end & 0x1F)));
}

void vga_disable_cursor(void)
{
    vga_outb(VGA_CRTC_INDEX, 0x0A);
    vga_outb(VGA_CRTC_DATA, 0x20);
}

void vga_update_cursor(void)
{
    vga_set_cursor(terminal_row, terminal_column);
}

void vga_set_statusbar_enabled(int enabled)
{
    g_statusbar_enabled = enabled ? 1 : 0;

    /* FIX: previously this only ever touched 0xB8000 bookkeeping
     * (terminal_row/top_row()), which is invisible the moment gterm
     * owns the display - the VESA framebuffer is what's actually
     * scanned out, not VGA text memory. Delegating to gterm's own
     * reserved-row mechanism when it's active means the status bar
     * enable/disable call actually has an on-screen effect regardless
     * of which backend is live, instead of silently doing nothing
     * under gterm while still reporting success. */
    if (gterm_is_active()) {
        gterm_set_statusbar_enabled(g_statusbar_enabled);
        return;
    }

    if (terminal_row < top_row())
    {
        terminal_row = top_row();
        terminal_column = 0;
        vga_update_cursor();
    }
}

void vga_draw_statusbar(const char *text)
{
    if (!text) text = "";

    /* Same reasoning as vga_set_statusbar_enabled(): writes to
     * 0xB8000 never reach the screen once gterm is active, so this
     * must delegate rather than draw into memory nobody scans out. */
    if (gterm_is_active()) {
        gterm_draw_statusbar(text);
        graphics_present();
        return;
    }

    uint8_t bar_color = (uint8_t)((VGA_BLUE << 4) | VGA_WHITE);
    uint16_t saved_color = terminal_color;
    terminal_color = bar_color;

    int i = 0;
    for (; text[i] && i < VGA_WIDTH; i++)
        VGA_MEMORY[i] = vga_entry(text[i]);
    for (; i < VGA_WIDTH; i++)
        VGA_MEMORY[i] = vga_entry(' ');

    terminal_color = (uint8_t)saved_color;
    vga_update_cursor();
}

static void vga_scroll(void)
{
    if (terminal_row < VGA_HEIGHT)
        return;

    uint16_t top = top_row();

    for (uint32_t y = (uint32_t)top + 1; y < VGA_HEIGHT; y++)
        for (uint32_t x = 0; x < VGA_WIDTH; x++)
            VGA_MEMORY[(y - 1) * VGA_WIDTH + x] = VGA_MEMORY[y * VGA_WIDTH + x];

    for (uint32_t x = 0; x < VGA_WIDTH; x++)
        VGA_MEMORY[(VGA_HEIGHT - 1) * VGA_WIDTH + x] = vga_entry(' ');

    terminal_row = VGA_HEIGHT - 1;
}

void vga_init(void)
{
    terminal_row = 0;
    terminal_column = 0;
    vga_clear();
    vga_enable_cursor(14, 15);
    vga_update_cursor();
}

void vga_clear(void)
{
    if (gterm_is_active()) {
        gterm_clear();
        graphics_present();
        return;
    }

    uint16_t top = top_row();

    for (uint32_t y = top; y < VGA_HEIGHT; y++)
        for (uint32_t x = 0; x < VGA_WIDTH; x++)
            VGA_MEMORY[y * VGA_WIDTH + x] = vga_entry(' ');

    terminal_row = top;
    terminal_column = 0;
    vga_update_cursor();
}

void vga_set_color(vga_color_t fg, vga_color_t bg)
{
    terminal_color = (uint8_t)(((uint8_t)bg << 4) | (uint8_t)fg);

    if (gterm_is_active()) {
        gterm_set_color((uint8_t)fg, (uint8_t)bg);
    }
}

void vga_putc(char c)
{

    if (gterm_is_active()) {
        gterm_putc(c);
        return;
    }

    if (terminal_row < top_row())
    {
        terminal_row = top_row();
        terminal_column = 0;
    }

    switch (c)
    {
    case '\n':
        terminal_column = 0;
        terminal_row++;
        vga_scroll();
        vga_update_cursor();
        return;

    case '\r':
        terminal_column = 0;
        vga_update_cursor();
        return;

    case '\b':
        if (terminal_column > 0)
        {
            terminal_column--;
        }
        else if (terminal_row > top_row())
        {
            terminal_row--;
            terminal_column = VGA_WIDTH - 1;
        }
        else
        {
            vga_update_cursor();
            return;
        }

        VGA_MEMORY[terminal_row * VGA_WIDTH + terminal_column] = vga_entry(' ');
        vga_update_cursor();
        return;

    default:
        break;
    }

    VGA_MEMORY[terminal_row * VGA_WIDTH + terminal_column] = vga_entry(c);
    terminal_column++;

    if (terminal_column >= VGA_WIDTH)
    {
        terminal_column = 0;
        terminal_row++;
        vga_scroll();
    }

    vga_update_cursor();
}

void vga_write(const char* str)
{
    if (!str) return;
    while (*str)
        vga_putc(*str++);

    /* graphics_core.c now draws into an off-screen back buffer (see
     * graphics_present()'s header comment) so windows/cursor don't
     * flicker in the desktop's per-frame redraw loop - but the boot
     * log and shell prompt run through this same gterm_is_active()
     * path (see vga_putc() above) OUTSIDE that loop, so without this
     * they'd draw into the back buffer and never actually reach the
     * screen. One flush per string keeps text appearing exactly as
     * before - desktop.c still does its own per-frame flush on top of
     * this, so this line is a no-op cost-wise while the desktop is
     * running (same buffer, redundant copy is cheap compared to a
     * frame's worth of drawing already done). */
    if (gterm_is_active()) graphics_present();
}

static const char vga_hex_digits[] = "0123456789ABCDEF";

void vga_write_hex(uint64_t value)
{
    char buf[17];
    for (int i = 0; i < 16; i++)
        buf[i] = vga_hex_digits[(value >> (60 - i * 4)) & 0xF];
    buf[16] = '\0';
    vga_write(buf);
}

void vga_write_dec(uint64_t value)
{
    if (value == 0)
    {
        vga_putc('0');
        return;
    }

    char buffer[32];
    int index = 0;
    while (value > 0)
    {
        buffer[index++] = (char)('0' + (value % 10));
        value /= 10;
    }
    while (index > 0)
        vga_putc(buffer[--index]);
}