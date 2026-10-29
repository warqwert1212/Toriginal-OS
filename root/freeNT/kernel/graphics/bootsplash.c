/* bootsplash.c */
#include "bootsplash.h"
#include "graphics_core.h"
#include "graphics_2d.h"
#include "gfx_terminal.h"
#include "png.h"
#include "serial.h"

extern const uint8_t g_trbl_logo_png[];
extern const uint64_t g_trbl_logo_png_size;

#define BAR_W 320u
#define BAR_H 8u

static int      g_splash_active = 0;
static uint32_t g_bar_x, g_bar_y;
static int      g_last_pct = -1;

static color_t track_color(void) { return graphics_rgb(0x30, 0x30, 0x30); }
static color_t fill_color(void)  { return graphics_rgb(0xFF, 0x7A, 0x00); }

int bootsplash_active(void) { return g_splash_active; }

void bootsplash_start(void) {
    if (g_splash_active || !graphics_is_available()) return;

    uint32_t sw = g_framebuffer.width, sh = g_framebuffer.height;
    graphics_clear_screen(GRAPHICS_COLOR_BLACK);

    uint32_t orb_w = 0, orb_h = 0;
    png_image_t decoded = {0};
    gfx2d_surface_t surf = {0};
    int have_orb = 0;

    if (png_decode(g_trbl_logo_png, (size_t)g_trbl_logo_png_size, &decoded) == PNG_OK && decoded.pixels) {
        surf = gfx2d_surface_create(decoded.width, decoded.height);
        if (surf.pixels) {
            uint8_t ch = decoded.channels;
            for (uint32_t y = 0; y < decoded.height; y++) {
                const uint8_t *row = decoded.pixels + (size_t)y * decoded.width * ch;
                for (uint32_t x = 0; x < decoded.width; x++) {
                    const uint8_t *px = row + (size_t)x * ch;
                    color_t c = (ch == 4) ? graphics_argb(px[3], px[0], px[1], px[2])
                                          : graphics_argb(255, px[0], px[1], px[2]);
                    gfx2d_surface_set_pixel(&surf, x, y, c);
                }
            }
            orb_w = decoded.width; orb_h = decoded.height;
            have_orb = 1;
        }
    }

    uint32_t block_h = orb_h + 40 + BAR_H;
    uint32_t top = (sh > block_h) ? (sh - block_h) / 2 : 0;

    if (have_orb) {
        gfx2d_blit_alpha(&surf, (int32_t)((sw - orb_w) / 2), (int32_t)top);
        gfx2d_surface_destroy(&surf);
    }
    if (decoded.pixels) png_free(&decoded);

    g_bar_x = (sw > BAR_W) ? (sw - BAR_W) / 2 : 0;
    g_bar_y = top + orb_h + 40;
    graphics_fill_rect(g_bar_x, g_bar_y, BAR_W, BAR_H, track_color());
    graphics_present();

    g_splash_active = 1;
    g_last_pct = 0;
    serial_write("[SPLASH] Boot splash up\n");
}

void bootsplash_progress(int percent) {
    if (!g_splash_active) return;
    if (percent < 0) percent = 0;
    if (percent > 100) percent = 100;
    if (percent <= g_last_pct) return;
    g_last_pct = percent;

    uint32_t fill = (BAR_W * (uint32_t)percent) / 100u;
    if (fill) graphics_fill_rect(g_bar_x, g_bar_y, fill, BAR_H, fill_color());
    graphics_present();
}

void bootsplash_relayout(void) {
    if (!g_splash_active) return;
    int pct = g_last_pct;
    g_splash_active = 0;
    g_last_pct = -1;
    bootsplash_start();
    if (pct > 0) bootsplash_progress(pct);
}

void bootsplash_finish(void) {
    if (!g_splash_active) return;
    g_splash_active = 0;
    graphics_clear_screen(GRAPHICS_COLOR_BLACK);
    if (gterm_is_active()) gterm_clear();
    graphics_present();
}
