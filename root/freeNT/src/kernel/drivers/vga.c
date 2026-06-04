// ==============================================================================
// VGA.C - Native Hardware Screen Driver
// ==============================================================================
#include <stdint.h>
#include <stddef.h>

// VGA text mode physical memory address (Identity mapped in entry.s)
volatile uint16_t* vga_buffer = (uint16_t*)0xB8000;
const size_t VGA_WIDTH = 80;
const size_t VGA_HEIGHT = 25;

size_t terminal_row = 0;
size_t terminal_column = 0;
uint8_t terminal_color = 0x0F; // White text (0xF) on Black background (0x0)

void vga_clear(void) {
    for (size_t y = 0; y < VGA_HEIGHT; y++) {
        for (size_t x = 0; x < VGA_WIDTH; x++) {
            const size_t index = y * VGA_WIDTH + x;
            vga_buffer[index] = ((uint16_t)terminal_color << 8) | ' ';
        }
    }
    terminal_row = 0;
    terminal_column = 0;
}

void vga_putchar(char c) {
    if (c == '\n') {
        terminal_column = 0;
        if (++terminal_row == VGA_HEIGHT) terminal_row = 0; // Simple wrap-around
        return;
    }

    const size_t index = terminal_row * VGA_WIDTH + terminal_column;
    vga_buffer[index] = ((uint16_t)terminal_color << 8) | c;

    if (++terminal_column == VGA_WIDTH) {
        terminal_column = 0;
        if (++terminal_row == VGA_HEIGHT) terminal_row = 0;
    }
}

void print_vga(const char* str) {
    for (size_t i = 0; str[i] != '\0'; i++) {
        vga_putchar(str[i]);
    }
}