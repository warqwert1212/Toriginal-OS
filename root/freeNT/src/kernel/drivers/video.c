// ==============================================================================
// VIDEO.C - High-Performance Linear Framebuffer Graphics Driver
// ==============================================================================
#include <stdint.h>

static uint32_t* framebuffer = (uint32_t*)0; // Will be set by Multiboot structure parsing
static uint32_t fb_width = 0;
static uint32_t fb_height = 0;
static uint32_t fb_pitch = 0;

void init_graphics(uint64_t addr, uint32_t width, uint32_t height, uint32_t pitch) {
    framebuffer = (uint32_t*)addr;
    fb_width = width;
    fb_height = height;
    fb_pitch = pitch;
}

void put_pixel(uint32_t x, uint32_t y, uint32_t color) {
    if (x >= fb_width || y >= fb_height) return;
    
    // Pitch is in bytes, convert row indexing to 32-bit pixel offsets
    uint32_t index = (y * (fb_pitch / 4)) + x;
    framebuffer[index] = color;
}

void draw_rect(uint32_t x, uint32_t y, uint32_t w, uint32_t h, uint32_t color) {
    for (uint32_t i = 0; i < h; i++) {
        for (uint32_t j = 0; j < w; j++) {
            put_pixel(x + j, y + i, color);
        }
    }
}

void clear_screen(uint32_t color) {
    draw_rect(0, 0, fb_width, fb_height, color);
}