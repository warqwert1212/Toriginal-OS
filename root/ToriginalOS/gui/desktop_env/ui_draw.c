#include "ui_draw.h"
#include "font8x8_min.h"

void ui_fill_rect(uint32_t *fb, int32_t fb_w, int32_t fb_h,
                   int32_t x, int32_t y, int32_t w, int32_t h, uint32_t color) {
    for (int32_t yy = y; yy < y + h; yy++) {
        if (yy < 0 || yy >= fb_h) continue;
        for (int32_t xx = x; xx < x + w; xx++) {
            if (xx < 0 || xx >= fb_w) continue;
            fb[yy * fb_w + xx] = color;
        }
    }
}

void ui_draw_char(uint32_t *fb, int32_t fb_w, int32_t fb_h,
                   int32_t px, int32_t py, char c, uint32_t color) {
    const uint8_t *rows = font8x8_lookup(c);
    for (int ry = 0; ry < 8; ry++) {
        int32_t y = py + ry;
        if (y < 0 || y >= fb_h) continue;
        for (int rx = 0; rx < 8; rx++) {
            if (!(rows[ry] & (0x80 >> rx))) continue;
            int32_t x = px + rx;
            if (x < 0 || x >= fb_w) continue;
            fb[y * fb_w + x] = color;
        }
    }
}

void ui_draw_text(uint32_t *fb, int32_t fb_w, int32_t fb_h,
                   int32_t x, int32_t y, const char *s, uint32_t color) {
    int32_t cx = x;
    while (*s) {
        ui_draw_char(fb, fb_w, fb_h, cx, y, *s, color);
        cx += 9;
        s++;
    }
}
