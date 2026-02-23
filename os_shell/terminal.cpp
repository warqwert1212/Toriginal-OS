#include "terminal.hpp"
#include "io.hpp"
#include "keyboard.hpp"

static inline uint16_t vga_entry(char c, uint8_t color) {
    return (uint16_t)c | ((uint16_t)color << 8);
}

Terminal::Terminal()
    : vram((uint16_t*)0xB8000),
      cursor_x(0),
      cursor_y(0),
      color(0x07) // light gray on black
{
    clear();
}

void Terminal::clear() {
    for (int i = 0; i < 80 * 25; ++i)
        vram[i] = vga_entry(' ', color);

    cursor_x = 0;
    cursor_y = 0;
    update_hw_cursor();
}

void Terminal::set_color(uint8_t fg, uint8_t bg) {
    color = (bg << 4) | (fg & 0x0F);
}

void Terminal::putchar(char c) {
    if (c == '\n') {
        cursor_x = 0;
        cursor_y++;
    } else if (c == '\r') {
        cursor_x = 0;
    } else {
        vram[cursor_y * 80 + cursor_x] = vga_entry(c, color);
        cursor_x++;
        if (cursor_x >= 80) {
            cursor_x = 0;
            cursor_y++;
        }
    }

    if (cursor_y >= 25)
        scroll();

    update_hw_cursor();
}

void Terminal::write(const char* str) {
    while (*str) putchar(*str++);
}

void Terminal::writeln(const char* str) {
    write(str);
    putchar('\n');
}

void Terminal::scroll() {
    for (int y = 1; y < 25; ++y)
        for (int x = 0; x < 80; ++x)
            vram[(y - 1) * 80 + x] = vram[y * 80 + x];

    for (int x = 0; x < 80; ++x)
        vram[24 * 80 + x] = vga_entry(' ', color);

    cursor_y = 24;
}

void Terminal::move_cursor(uint8_t x, uint8_t y) {
    cursor_x = x;
    cursor_y = y;
    update_hw_cursor();
}

void Terminal::update_hw_cursor() {
    uint16_t pos = cursor_y * 80 + cursor_x;

    outb(0x3D4, 0x0F);
    outb(0x3D5, pos & 0xFF);
    outb(0x3D4, 0x0E);
    outb(0x3D5, (pos >> 8) & 0xFF);
}

int Terminal::read_line(char* buffer, int max_len) {
    int len = 0;

    while (true) {
        char c = keyboard_getchar();

        if (c == '\n' || c == '\r') {
            putchar('\n');
            buffer[len] = '\0';
            return len;
        }

        if (c == '\b') {
            if (len > 0) {
                len--;
                putchar('\b');
                putchar(' ');
                putchar('\b');
            }
            continue;
        }

        if (len < max_len - 1) {
            buffer[len++] = c;
            putchar(c);
        }
    }
}
