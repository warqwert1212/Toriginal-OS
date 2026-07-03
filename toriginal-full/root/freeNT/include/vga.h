#ifndef FREENT_VGA_H
#define FREENT_VGA_H

#include "types.h"

typedef enum
{
    VGA_BLACK = 0,
    VGA_BLUE = 1,
    VGA_GREEN = 2,
    VGA_CYAN = 3,
    VGA_RED = 4,
    VGA_MAGENTA = 5,
    VGA_BROWN = 6,
    VGA_LIGHT_GREY = 7,
    VGA_DARK_GREY = 8,
    VGA_LIGHT_BLUE = 9,
    VGA_LIGHT_GREEN = 10,
    VGA_LIGHT_CYAN = 11,
    VGA_LIGHT_RED = 12,
    VGA_LIGHT_MAGENTA = 13,
    VGA_YELLOW = 14,
    VGA_WHITE = 15
} vga_color_t;

void vga_init(void);

void vga_clear(void);

void vga_set_color(
    vga_color_t fg,
    vga_color_t bg
);

void vga_putc(char c);

void vga_write(const char* str);

void vga_write_hex(uint64_t value);

void vga_write_dec(uint64_t value);

void vga_set_cursor(uint16_t row, uint16_t col);
void vga_enable_cursor(uint8_t cursor_start, uint8_t cursor_end);
void vga_disable_cursor(void);
void vga_update_cursor(void);

void vga_set_statusbar_enabled(int enabled);
void vga_draw_statusbar(const char *text);

#endif
