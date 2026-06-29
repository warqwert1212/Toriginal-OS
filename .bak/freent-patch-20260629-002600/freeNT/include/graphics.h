/* Graphics framework for freeNT - VESA/Framebuffer support */
#ifndef GRAPHICS_H
#define GRAPHICS_H

#include <stdint.h>
#include <stddef.h>

/* Color type - 32-bit ARGB */
typedef uint32_t color_t;

/* Graphics modes */
typedef enum {
    GRAPHICS_MODE_VGA,
    GRAPHICS_MODE_VESA_16BIT,
    GRAPHICS_MODE_VESA_32BIT,
} graphics_mode_t;

/* Framebuffer information */
typedef struct {
    uint32_t width;
    uint32_t height;
    uint32_t depth;        /* bits per pixel */
    uint32_t pitch;        /* bytes per scanline */
    uint8_t *framebuffer;  /* physical address */
    graphics_mode_t mode;
} framebuffer_t;

/* Global framebuffer */
extern framebuffer_t g_framebuffer;

/* Initialization */
int graphics_init(void);
int graphics_set_mode(graphics_mode_t mode, uint32_t width, uint32_t height);
int graphics_is_available(void);

/* Drawing primitives */
void graphics_clear_screen(color_t color);
void graphics_draw_pixel(uint32_t x, uint32_t y, color_t color);
void graphics_draw_rect(uint32_t x, uint32_t y, uint32_t width, uint32_t height, color_t color);
void graphics_fill_rect(uint32_t x, uint32_t y, uint32_t width, uint32_t height, color_t color);
void graphics_draw_line(uint32_t x1, uint32_t y1, uint32_t x2, uint32_t y2, color_t color);
void graphics_draw_circle(uint32_t x, uint32_t y, uint32_t radius, color_t color);

/* Color utilities */
static inline color_t graphics_rgb(uint8_t r, uint8_t g, uint8_t b) {
    return 0xFF000000 | ((r << 16) | (g << 8) | b);
}

static inline color_t graphics_argb(uint8_t a, uint8_t r, uint8_t g, uint8_t b) {
    return ((a << 24) | (r << 16) | (g << 8) | b);
}

/* Standard colors */
#define GRAPHICS_COLOR_BLACK     graphics_rgb(0, 0, 0)
#define GRAPHICS_COLOR_WHITE     graphics_rgb(255, 255, 255)
#define GRAPHICS_COLOR_RED       graphics_rgb(255, 0, 0)
#define GRAPHICS_COLOR_GREEN     graphics_rgb(0, 255, 0)
#define GRAPHICS_COLOR_BLUE      graphics_rgb(0, 0, 255)
#define GRAPHICS_COLOR_CYAN      graphics_rgb(0, 255, 255)
#define GRAPHICS_COLOR_MAGENTA   graphics_rgb(255, 0, 255)
#define GRAPHICS_COLOR_YELLOW    graphics_rgb(255, 255, 0)

#endif /* GRAPHICS_H */
