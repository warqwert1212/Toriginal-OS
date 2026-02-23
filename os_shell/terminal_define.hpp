#ifndef TERMINAL_HPP
#define TERMINAL_HPP

#include <stdint.h>

class Terminal {
public:
    Terminal();

    void clear();
    void set_color(uint8_t fg, uint8_t bg);
    void putchar(char c);
    void write(const char* str);
    void writeln(const char* str);

    void move_cursor(uint8_t x, uint8_t y);
    int  read_line(char* buffer, int max_len);

private:
    void scroll();
    void update_hw_cursor();

    uint16_t* vram;
    uint8_t cursor_x;
    uint8_t cursor_y;
    uint8_t color;
};

#endif