#include "vga.h"

#define VGA_WIDTH 80
#define VGA_HEIGHT 25

static volatile uint16_t* const VGA_MEMORY =
    (uint16_t*)0xB8000;

static uint16_t terminal_row = 0;
static uint16_t terminal_column = 0;

static uint8_t terminal_color =
    (VGA_BLACK << 4) | VGA_WHITE;

/* Top status bar — when enabled, row 0 is reserved for it and the
 * scrollable text region becomes rows [1, VGA_HEIGHT). */
static int g_statusbar_enabled = 0;
static int top_row(void) { return g_statusbar_enabled ? 1 : 0; }

void vga_set_statusbar_enabled(int enabled) {
    g_statusbar_enabled = enabled ? 1 : 0;
    if (terminal_row < (uint16_t)top_row()) terminal_row = (uint16_t)top_row();
}

/* Draw the status bar text at row 0 without disturbing the cursor or
 * scroll state of the main text region. */
void vga_draw_statusbar(const char *text) {
    uint8_t bar_color = (VGA_BLUE << 4) | VGA_WHITE;
    int i = 0;
    for (; text[i] && i < VGA_WIDTH; i++) {
        VGA_MEMORY[i] = ((uint16_t)bar_color << 8) | (uint8_t)text[i];
    }
    for (; i < VGA_WIDTH; i++) {
        VGA_MEMORY[i] = ((uint16_t)bar_color << 8) | ' ';
    }
}

static void vga_scroll(void)
{
    if (terminal_row < VGA_HEIGHT)
        return;

    int top = top_row();
    for (uint32_t y = (uint32_t)top + 1; y < VGA_HEIGHT; y++)
    {
        for (uint32_t x = 0; x < VGA_WIDTH; x++)
        {
            VGA_MEMORY[(y - 1) * VGA_WIDTH + x] =
                VGA_MEMORY[y * VGA_WIDTH + x];
        }
    }

    for (uint32_t x = 0; x < VGA_WIDTH; x++)
    {
        VGA_MEMORY[(VGA_HEIGHT - 1) * VGA_WIDTH + x] =
            ((uint16_t)terminal_color << 8) | ' ';
    }

    terminal_row = VGA_HEIGHT - 1;
}

void vga_init(void)
{
    terminal_row = 0;
    terminal_column = 0;
    vga_clear();
    vga_enable_cursor(14, 15); /* underline cursor, lines 14-15 of character cell */
}

void vga_clear(void)
{
    int top = top_row();
    for (uint32_t y = (uint32_t)top; y < VGA_HEIGHT; y++)
    {
        for (uint32_t x = 0; x < VGA_WIDTH; x++)
        {
            VGA_MEMORY[y * VGA_WIDTH + x] =
                ((uint16_t)terminal_color << 8) | ' ';
        }
    }

    terminal_row = (uint16_t)top;
    terminal_column = 0;
}

void vga_set_color(
    vga_color_t fg,
    vga_color_t bg)
{
    terminal_color =
        ((uint8_t)bg << 4) |
        (uint8_t)fg;
}

void vga_putc(char c)
{
    if (terminal_row < (uint16_t)top_row()) terminal_row = (uint16_t)top_row();

    if (c == '\n')
    {
        terminal_column = 0;
        terminal_row++;
        vga_scroll();
        vga_set_cursor(terminal_row, terminal_column);
        return;
    }

    if (c == '\r')
    {
        terminal_column = 0;
        vga_set_cursor(terminal_row, terminal_column);
        return;
    }

    VGA_MEMORY[
        terminal_row * VGA_WIDTH +
        terminal_column
    ] =
        ((uint16_t)terminal_color << 8) |
        (uint8_t)c;

    terminal_column++;

    if (terminal_column >= VGA_WIDTH)
    {
        terminal_column = 0;
        terminal_row++;
        vga_scroll();
    }
    vga_set_cursor(terminal_row, terminal_column);
}

void vga_write(const char* str)
{
    while (*str)
    {
        vga_putc(*str++);
    }
}

static const char vga_hex_digits[] = "0123456789ABCDEF";

void vga_write_hex(uint64_t value)
{
    char buf[17];
    for (int i = 0; i < 16; i++)
        buf[i] = vga_hex_digits[(value >> (60 - i*4)) & 0xF];
    buf[16] = '\0';
    vga_write(buf);
}

void vga_write_dec(uint64_t value)
{
    char buffer[32];
    int index = 0;

    if (value == 0)
    {
        vga_putc('0');
        return;
    }

    while (value > 0)
    {
        buffer[index++] =
            '0' + (value % 10);

        value /= 10;
    }

    while (index > 0)
    {
        vga_putc(buffer[--index]);
    }
}

/* ── Hardware text-mode cursor control ───────────────────────────────────── */

static inline void vga_outb(uint16_t port, uint8_t val) {
    __asm__ volatile("outb %0, %1" :: "a"(val), "Nd"(port));
}
static inline uint8_t vga_inb(uint16_t port) {
    uint8_t v; __asm__ volatile("inb %1, %0" : "=a"(v) : "Nd"(port)); return v;
}

void vga_set_cursor(uint16_t row, uint16_t col) {
    uint16_t pos = row * VGA_WIDTH + col;
    vga_outb(0x3D4, 0x0F); vga_outb(0x3D5, (uint8_t)(pos & 0xFF));
    vga_outb(0x3D4, 0x0E); vga_outb(0x3D5, (uint8_t)(pos >> 8));
}

void vga_enable_cursor(uint8_t start, uint8_t end) {
    vga_outb(0x3D4, 0x0A); vga_outb(0x3D5, (uint8_t)((vga_inb(0x3D5) & 0xC0) | start));
    vga_outb(0x3D4, 0x0B); vga_outb(0x3D5, (uint8_t)((vga_inb(0x3D5) & 0xE0) | end));
}

void vga_disable_cursor(void) {
    vga_outb(0x3D4, 0x0A); vga_outb(0x3D5, 0x20);
}

void vga_update_cursor(void) {
    vga_set_cursor(terminal_row, terminal_column);
}
