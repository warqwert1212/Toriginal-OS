/* =============================================================================
 * vga.c — VGA text-mode (0xB8000) driver
 *
 * Owns: the 80x25 character buffer, the blinking hardware cursor (CRTC
 * registers on 0x3D4/0x3D5), colour state, and the optional single-row
 * status bar pinned to the top of the screen.
 *
 * Rewritten from scratch for clarity and to make the row/column/scroll
 * bookkeeping impossible to get out of sync with the hardware cursor.
 * ============================================================================= */

#include "vga.h"

#define VGA_WIDTH   80
#define VGA_HEIGHT  25

/* CRTC (cursor) I/O ports */
#define VGA_CRTC_INDEX 0x3D4
#define VGA_CRTC_DATA  0x3D5

static volatile uint16_t* const VGA_MEMORY = (uint16_t*)0xB8000;

/* Logical cursor position within the text buffer. Always kept in the
 * range [top_row(), VGA_HEIGHT) x [0, VGA_WIDTH) — every function that
 * touches these clamps them before returning, so nothing downstream
 * ever has to defend against an out-of-range value. */
static uint16_t terminal_row    = 0;
static uint16_t terminal_column = 0;

static uint8_t terminal_color = (uint8_t)((VGA_BLACK << 4) | VGA_WHITE);

/* When enabled, row 0 is reserved for the status bar and the scrollable
 * text region is rows [1, VGA_HEIGHT). */
static int g_statusbar_enabled = 0;

static inline uint16_t top_row(void)
{
    return g_statusbar_enabled ? 1 : 0;
}

static inline uint16_t vga_entry(char c)
{
    return ((uint16_t)terminal_color << 8) | (uint8_t)c;
}

/* ── low-level port I/O ─────────────────────────────────────────────────── */

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

/* ── hardware cursor ────────────────────────────────────────────────────── */

void vga_set_cursor(uint16_t row, uint16_t col)
{
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

/* ── status bar ─────────────────────────────────────────────────────────── */

void vga_set_statusbar_enabled(int enabled)
{
    g_statusbar_enabled = enabled ? 1 : 0;

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

    uint8_t bar_color = (uint8_t)((VGA_BLUE << 4) | VGA_WHITE);
    uint16_t saved_color = terminal_color;
    terminal_color = bar_color;

    int i = 0;
    for (; text[i] && i < VGA_WIDTH; i++)
        VGA_MEMORY[i] = vga_entry(text[i]);
    for (; i < VGA_WIDTH; i++)
        VGA_MEMORY[i] = vga_entry(' ');

    terminal_color = (uint8_t)saved_color;
    /* Status bar redraw never moves the real cursor. */
    vga_update_cursor();
}

/* ── scrolling ───────────────────────────────────────────────────────────── */

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

/* ── init / clear / color ───────────────────────────────────────────────── */

void vga_init(void)
{
    terminal_row = 0;
    terminal_column = 0;
    vga_clear();
    vga_enable_cursor(14, 15); /* thin underline cursor */
    vga_update_cursor();
}

void vga_clear(void)
{
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
}

/* ── character output ───────────────────────────────────────────────────── */

void vga_putc(char c)
{
    /* Never let the cursor drift above the scrollable region (can happen
     * right after the status bar is toggled on). */
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
            /* Already at the top-left of the text region — nothing to do. */
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
}

/* ── numeric printing helpers ───────────────────────────────────────────── */

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
