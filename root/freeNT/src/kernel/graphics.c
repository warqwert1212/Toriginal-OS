/* Graphics implementation for freeNT - Framebuffer rendering */

#include "graphics.h"
#include "string.h"
#include <stddef.h>

/* Global framebuffer state */
framebuffer_t g_framebuffer = {
    .width = 0,
    .height = 0,
    .depth = 0,
    .pitch = 0,
    .framebuffer = NULL,
    .mode = GRAPHICS_MODE_VGA
};

/* Initialize graphics subsystem */
int graphics_init(void) {
    /* Default to VGA mode (640x480) */
    /* In a real implementation, we would detect VESA modes from GRUB multiboot */
    g_framebuffer.width = 640;
    g_framebuffer.height = 480;
    g_framebuffer.depth = 32;
    g_framebuffer.pitch = g_framebuffer.width * 4;
    g_framebuffer.framebuffer = (uint8_t *)0xE0000000;  /* Default VESA framebuffer address */
    g_framebuffer.mode = GRAPHICS_MODE_VESA_32BIT;
    
    return 1;
}

/* Set graphics mode */
int graphics_set_mode(graphics_mode_t mode, uint32_t width, uint32_t height) {
    g_framebuffer.mode = mode;
    g_framebuffer.width = width;
    g_framebuffer.height = height;
    
    switch (mode) {
        case GRAPHICS_MODE_VGA:
            g_framebuffer.depth = 8;
            g_framebuffer.pitch = width;
            break;
        case GRAPHICS_MODE_VESA_16BIT:
            g_framebuffer.depth = 16;
            g_framebuffer.pitch = width * 2;
            break;
        case GRAPHICS_MODE_VESA_32BIT:
            g_framebuffer.depth = 32;
            g_framebuffer.pitch = width * 4;
            break;
        default:
            return 0;
    }
    
    return 1;
}

/* Check if graphics is available */
int graphics_is_available(void) {
    return g_framebuffer.framebuffer != NULL && g_framebuffer.width > 0;
}

/* Clear screen to color */
void graphics_clear_screen(color_t color) {
    if (!graphics_is_available()) return;
    
    uint32_t *fb = (uint32_t *)g_framebuffer.framebuffer;
    uint32_t pixels = g_framebuffer.width * g_framebuffer.height;
    
    for (uint32_t i = 0; i < pixels; i++) {
        fb[i] = color;
    }
}

/* Draw single pixel */
void graphics_draw_pixel(uint32_t x, uint32_t y, color_t color) {
    if (!graphics_is_available()) return;
    if (x >= g_framebuffer.width || y >= g_framebuffer.height) return;
    
    uint32_t *fb = (uint32_t *)g_framebuffer.framebuffer;
    uint32_t offset = (y * g_framebuffer.width) + x;
    fb[offset] = color;
}

/* Draw rectangle outline */
void graphics_draw_rect(uint32_t x, uint32_t y, uint32_t width, uint32_t height, color_t color) {
    if (!graphics_is_available()) return;
    
    /* Top and bottom lines */
    for (uint32_t i = 0; i < width; i++) {
        graphics_draw_pixel(x + i, y, color);
        graphics_draw_pixel(x + i, y + height - 1, color);
    }
    
    /* Left and right lines */
    for (uint32_t i = 0; i < height; i++) {
        graphics_draw_pixel(x, y + i, color);
        graphics_draw_pixel(x + width - 1, y + i, color);
    }
}

/* Fill rectangle */
void graphics_fill_rect(uint32_t x, uint32_t y, uint32_t width, uint32_t height, color_t color) {
    if (!graphics_is_available()) return;
    
    uint32_t *fb = (uint32_t *)g_framebuffer.framebuffer;
    
    for (uint32_t row = 0; row < height; row++) {
        for (uint32_t col = 0; col < width; col++) {
            uint32_t px = x + col;
            uint32_t py = y + row;
            if (px < g_framebuffer.width && py < g_framebuffer.height) {
                uint32_t offset = (py * g_framebuffer.width) + px;
                fb[offset] = color;
            }
        }
    }
}

/* Draw line (Bresenham's algorithm) */
void graphics_draw_line(uint32_t x1, uint32_t y1, uint32_t x2, uint32_t y2, color_t color) {
    if (!graphics_is_available()) return;
    
    int dx = (x2 > x1) ? (x2 - x1) : (x1 - x2);
    int dy = (y2 > y1) ? (y2 - y1) : (y1 - y2);
    int sx = (x1 < x2) ? 1 : -1;
    int sy = (y1 < y2) ? 1 : -1;
    int err = dx - dy;
    
    int x = x1, y = y1;
    
    while (1) {
        graphics_draw_pixel(x, y, color);
        
        if (x == x2 && y == y2) break;
        
        int e2 = 2 * err;
        if (e2 > -dy) {
            err -= dy;
            x += sx;
        }
        if (e2 < dx) {
            err += dx;
            y += sy;
        }
    }
}

/* Draw circle (Midpoint circle algorithm) */
void graphics_draw_circle(uint32_t cx, uint32_t cy, uint32_t radius, color_t color) {
    if (!graphics_is_available()) return;
    
    int x = radius;
    int y = 0;
    int d = 3 - (2 * radius);
    
    while (x >= y) {
        graphics_draw_pixel(cx + x, cy + y, color);
        graphics_draw_pixel(cx - x, cy + y, color);
        graphics_draw_pixel(cx + x, cy - y, color);
        graphics_draw_pixel(cx - x, cy - y, color);
        graphics_draw_pixel(cx + y, cy + x, color);
        graphics_draw_pixel(cx - y, cy + x, color);
        graphics_draw_pixel(cx + y, cy - x, color);
        graphics_draw_pixel(cx - y, cy - x, color);
        
        if (d < 0) {
            d = d + (4 * y) + 6;
        } else {
            d = d + (4 * (y - x)) + 10;
            x--;
        }
        y++;
    }
}
