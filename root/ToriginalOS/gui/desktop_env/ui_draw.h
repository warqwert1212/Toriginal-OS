#ifndef _UI_DRAW_H
#define _UI_DRAW_H

#include <stdint.h>

static inline uint32_t ui_argb(uint8_t a, uint8_t r, uint8_t g, uint8_t b) {
    return ((uint32_t)a << 24) | ((uint32_t)r << 16) | ((uint32_t)g << 8) | b;
}

void ui_fill_rect(uint32_t *fb, int32_t fb_w, int32_t fb_h,
                   int32_t x, int32_t y, int32_t w, int32_t h, uint32_t color);

void ui_draw_char(uint32_t *fb, int32_t fb_w, int32_t fb_h,
                   int32_t px, int32_t py, char c, uint32_t color);

void ui_draw_text(uint32_t *fb, int32_t fb_w, int32_t fb_h,
                   int32_t x, int32_t y, const char *s, uint32_t color);

#endif
