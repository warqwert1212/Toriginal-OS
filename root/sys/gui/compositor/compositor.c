#include "compositor.h"
#include "wm.h"
#include <string.h>
#include <stdlib.h>
#include <math.h>

/* ==============================================================================
 * COMPOSITOR.C — Compositing Engine Implementation
 *
 * Renders WM windows + wallpaper + Aero glass effects to a single framebuffer.
 *
 * Aero Glass Algorithm:
 *   The "Aero glass" effect is a Gaussian blur of the region BEHIND the
 *   window frame, with a tint overlay and bright highlight. Steps:
 *
 *   1. Render wallpaper + lower windows to the final framebuffer
 *   2. For the current window's frame region:
 *      a. Extract the region behind it (wallpaper + windows below)
 *      b. Apply Gaussian blur (separable: horizontal then vertical)
 *      c. Overlay a semi-transparent tint (e.g., 200,220,255 at 50% alpha)
 *      d. Draw a bright 1px highlight on top/left edges
 *   3. Draw the window's client area on top
 *
 *   This creates the translucent glass look: you can see what's behind,
 *   but it's blurred and tinted, giving depth.
 *
 * Gaussian blur implementation: separable 1D convolution.
 *   - Horizontal pass: blur each row, write to temp buffer
 *   - Vertical pass: blur each column, write to final buffer
 *   - Kernel: Gaussian with sigma ~= radius/2, truncated at 3*sigma
 * ============================================================================== */

#define COMP_BLUR_RADIUS 5
#define COMP_BLUR_SIGMA  (COMP_BLUR_RADIUS / 2.0f)

typedef struct {
    uint32_t *framebuffer;      /* final ARGB8888 output */
    uint32_t *wallpaper;        /* wallpaper ARGB8888 (owned by compositor) */
    
    int32_t  screen_w, screen_h;
    int32_t  screen_pitch;
    
    int32_t  wallpaper_w, wallpaper_h;
} comp_state_t;

static comp_state_t g_comp = {0};

/* ─ Utility: get/set ARGB pixel ─────────────────────────────────────────── */

static inline uint32_t argb_pixel(uint8_t a, uint8_t r, uint8_t g, uint8_t b) {
    return ((uint32_t)a << 24) | ((uint32_t)r << 16) | ((uint32_t)g << 8) | (uint32_t)b;
}

static inline void argb_unpack(uint32_t px, uint8_t *a, uint8_t *r, uint8_t *g, uint8_t *b) {
    *a = (px >> 24) & 0xFF;
    *r = (px >> 16) & 0xFF;
    *g = (px >> 8) & 0xFF;
    *b = (px) & 0xFF;
}

/* Blend src over dst: dst' = src + dst*(1-src_alpha) */
static inline uint32_t argb_blend(uint32_t src, uint32_t dst) {
    uint8_t src_a, src_r, src_g, src_b;
    uint8_t dst_a, dst_r, dst_g, dst_b;
    
    argb_unpack(src, &src_a, &src_r, &src_g, &src_b);
    argb_unpack(dst, &dst_a, &dst_r, &dst_g, &dst_b);
    
    int alpha = src_a + 1;  /* 0-256 range for fixed-point */
    int inv_alpha = 257 - alpha;
    
    uint8_t out_a = src_a + ((dst_a * inv_alpha) >> 8);
    uint8_t out_r = src_r + ((dst_r * inv_alpha) >> 8);
    uint8_t out_g = src_g + ((dst_g * inv_alpha) >> 8);
    uint8_t out_b = src_b + ((dst_b * inv_alpha) >> 8);
    
    return argb_pixel(out_a, out_r, out_g, out_b);
}

/* ─ Gaussian blur (separable convolution) ──────────────────────────────── */

/* Build a Gaussian kernel and return the sum (for normalization). */
static float *comp_make_gaussian_kernel(int radius, float *out_sum) {
    float *kernel = (float *)malloc((2*radius + 1) * sizeof(float));
    if (!kernel) return NULL;
    
    float sigma = COMP_BLUR_SIGMA;
    float s = 1.0f / (sigma * sqrtf(2.0f * 3.14159265f));
    float sum = 0.0f;
    
    for (int i = -radius; i <= radius; i++) {
        float x = (float)i;
        float v = s * expf(-(x*x) / (2.0f*sigma*sigma));
        kernel[i + radius] = v;
        sum += v;
    }
    
    *out_sum = sum;
    return kernel;
}

/* Horizontal pass: blur each row, write to temp buffer. */
static void comp_blur_h(uint32_t *src, uint32_t *dst, int w, int h,
                        float *kernel, int radius, float norm) {
    for (int y = 0; y < h; y++) {
        for (int x = 0; x < w; x++) {
            float r = 0, g = 0, b = 0, a = 0;
            
            for (int i = -radius; i <= radius; i++) {
                int sx = x + i;
                if (sx < 0 || sx >= w) sx = (sx < 0) ? 0 : w-1;
                
                uint32_t px = src[y*w + sx];
                uint8_t pa, pr, pg, pb;
                argb_unpack(px, &pa, &pr, &pg, &pb);
                
                float k = kernel[i + radius];
                a += pa * k;
                r += pr * k;
                g += pg * k;
                b += pb * k;
            }
            
            dst[y*w + x] = argb_pixel((uint8_t)(a/norm), (uint8_t)(r/norm),
                                      (uint8_t)(g/norm), (uint8_t)(b/norm));
        }
    }
}

/* Vertical pass: blur each column, write to output. */
static void comp_blur_v(uint32_t *src, uint32_t *dst, int w, int h,
                        float *kernel, int radius, float norm) {
    for (int y = 0; y < h; y++) {
        for (int x = 0; x < w; x++) {
            float r = 0, g = 0, b = 0, a = 0;
            
            for (int i = -radius; i <= radius; i++) {
                int sy = y + i;
                if (sy < 0 || sy >= h) sy = (sy < 0) ? 0 : h-1;
                
                uint32_t px = src[sy*w + x];
                uint8_t pa, pr, pg, pb;
                argb_unpack(px, &pa, &pr, &pg, &pb);
                
                float k = kernel[i + radius];
                a += pa * k;
                r += pr * k;
                g += pg * k;
                b += pb * k;
            }
            
            dst[y*w + x] = argb_pixel((uint8_t)(a/norm), (uint8_t)(r/norm),
                                      (uint8_t)(g/norm), (uint8_t)(b/norm));
        }
    }
}

/* Blur a region in-place using a temp buffer. */
static void comp_blur_region(uint32_t *pixels, int w, int h,
                             int x0, int y0, int w_region, int h_region) {
    (void)h;  /* used implicitly through pixel arithmetic */
    if (w_region <= 0 || h_region <= 0) return;
    
    /* Extract the region to blur into a temp buffer */
    uint32_t *temp = (uint32_t *)malloc(w_region * h_region * sizeof(uint32_t));
    uint32_t *temp2 = (uint32_t *)malloc(w_region * h_region * sizeof(uint32_t));
    if (!temp || !temp2) {
        free(temp);
        free(temp2);
        return;
    }
    
    float kernel_sum;
    float *kernel = comp_make_gaussian_kernel(COMP_BLUR_RADIUS, &kernel_sum);
    if (!kernel) {
        free(temp);
        free(temp2);
        return;
    }
    
    /* Copy region to temp */
    for (int y = 0; y < h_region; y++) {
        memcpy(&temp[y * w_region],
               &pixels[(y0 + y) * w + x0],
               w_region * sizeof(uint32_t));
    }
    
    /* Blur horizontal, vertical */
    comp_blur_h(temp, temp2, w_region, h_region, kernel, COMP_BLUR_RADIUS, kernel_sum);
    comp_blur_v(temp2, temp, w_region, h_region, kernel, COMP_BLUR_RADIUS, kernel_sum);
    
    /* Copy blurred region back */
    for (int y = 0; y < h_region; y++) {
        memcpy(&pixels[(y0 + y) * w + x0],
               &temp[y * w_region],
               w_region * sizeof(uint32_t));
    }
    
    free(kernel);
    free(temp2);
    free(temp);
}

/* ─ Wallpaper tiling ───────────────────────────────────────────────────── */

static void comp_tile_wallpaper(void) {
    if (!g_comp.wallpaper) {
        return;
    }
    
    for (int y = 0; y < g_comp.screen_h; y++) {
        for (int x = 0; x < g_comp.screen_w; x++) {
            int wx = x % g_comp.wallpaper_w;
            int wy = y % g_comp.wallpaper_h;
            g_comp.framebuffer[y * g_comp.screen_w + x] =
                g_comp.wallpaper[wy * g_comp.wallpaper_w + wx];
        }
    }
}

/* ─ Window rendering ───────────────────────────────────────────────────── */

/* Blit a window's client area to the framebuffer. */
static void comp_blit_window_client(wm_window_t *win) {
    if (!win->pixels || !(win->flags & WM_WIN_VISIBLE)) {
        return;
    }
    
    int32_t fb_x = win->client_x;
    int32_t fb_y = win->client_y;
    int32_t w = win->client_w;
    int32_t h = win->client_h;
    
    /* Clamp to framebuffer bounds */
    if (fb_x < 0 || fb_y < 0 || fb_x + w > g_comp.screen_w ||
        fb_y + h > g_comp.screen_h) {
        /* Clip blit — for now, skip if out of bounds (simple case) */
        return;
    }
    
    for (int y = 0; y < h; y++) {
        for (int x = 0; x < w; x++) {
            uint32_t src_px = win->pixels[y * (win->pitch / 4) + x];
            uint32_t *dst_px = &g_comp.framebuffer[(fb_y + y) * g_comp.screen_w + (fb_x + x)];
            *dst_px = argb_blend(src_px, *dst_px);
        }
    }
}

/* Draw window title bar with text (simplified: just color for now) */
static void comp_draw_title_bar(wm_window_t *win) {
    if (!(win->flags & WM_WIN_DECORATED)) return;
    
    int32_t x = win->x;
    int32_t y = win->y;
    int32_t w = win->w;
    int32_t h = WM_TITLEBAR_HEIGHT;
    
    /* Title bar background: solid color (would be blurred in real Aero) */
    uint32_t color;
    if (win->flags & WM_WIN_FOCUSED) {
        color = argb_pixel(0xFF, 0x08, 0x37, 0x80);  /* Windows 7 active title blue */
    } else {
        color = argb_pixel(0xFF, 0x8E, 0x8E, 0x93);  /* inactive gray */
    }
    
    for (int yy = 0; yy < h; yy++) {
        if (y + yy < 0 || y + yy >= g_comp.screen_h) continue;
        for (int xx = 0; xx < w; xx++) {
            if (x + xx < 0 || x + xx >= g_comp.screen_w) continue;
            g_comp.framebuffer[(y + yy) * g_comp.screen_w + (x + xx)] = color;
        }
    }
}

/* Draw window borders */
static void comp_draw_borders(wm_window_t *win) {
    if (!(win->flags & WM_WIN_DECORATED)) return;
    
    int32_t x = win->x;
    int32_t y = win->y;
    int32_t w = win->w;
    int32_t h = win->h;
    
    uint32_t border_color = argb_pixel(0xFF, 0x40, 0x40, 0x40);
    uint32_t highlight = argb_pixel(0xFF, 0xFF, 0xFF, 0xFF);
    
    /* Top and left borders: bright highlight (Aero-like) */
    for (int i = 0; i < WM_BORDER_WIDTH; i++) {
        /* Top */
        if (y + i >= 0 && y + i < g_comp.screen_h) {
            for (int xx = 0; xx < w; xx++) {
                if (x + xx >= 0 && x + xx < g_comp.screen_w) {
                    g_comp.framebuffer[(y + i) * g_comp.screen_w + (x + xx)] =
                        (i == 0) ? highlight : border_color;
                }
            }
        }
        /* Left */
        if (x + i >= 0 && x + i < g_comp.screen_w) {
            for (int yy = 0; yy < h; yy++) {
                if (y + yy >= 0 && y + yy < g_comp.screen_h) {
                    g_comp.framebuffer[(y + yy) * g_comp.screen_w + (x + i)] =
                        (i == 0) ? highlight : border_color;
                }
            }
        }
    }
    
    /* Bottom and right borders: darker */
    for (int i = 0; i < WM_BORDER_WIDTH; i++) {
        /* Bottom */
        if (y + h - 1 - i >= 0 && y + h - 1 - i < g_comp.screen_h) {
            for (int xx = 0; xx < w; xx++) {
                if (x + xx >= 0 && x + xx < g_comp.screen_w) {
                    g_comp.framebuffer[(y + h - 1 - i) * g_comp.screen_w + (x + xx)] =
                        border_color;
                }
            }
        }
        /* Right */
        if (x + w - 1 - i >= 0 && x + w - 1 - i < g_comp.screen_w) {
            for (int yy = 0; yy < h; yy++) {
                if (y + yy >= 0 && y + yy < g_comp.screen_h) {
                    g_comp.framebuffer[(y + yy) * g_comp.screen_w + (x + w - 1 - i)] =
                        border_color;
                }
            }
        }
    }
}

/* ─ Public interface ───────────────────────────────────────────────────── */

void comp_init(int32_t screen_w, int32_t screen_h) {
    g_comp.screen_w = screen_w;
    g_comp.screen_h = screen_h;
    g_comp.screen_pitch = screen_w * 4;
    g_comp.framebuffer = (uint32_t *)malloc(screen_w * screen_h * sizeof(uint32_t));
    
    /* Default wallpaper: solid dark slate */
    g_comp.wallpaper = (uint32_t *)malloc(sizeof(uint32_t));
    g_comp.wallpaper[0] = argb_pixel(0xFF, 0x20, 0x20, 0x30);
    g_comp.wallpaper_w = 1;
    g_comp.wallpaper_h = 1;
}

void comp_shutdown(void) {
    free(g_comp.framebuffer);
    free(g_comp.wallpaper);
    memset(&g_comp, 0, sizeof(g_comp));
}

void comp_set_wallpaper(uint32_t *argb_pixels, int32_t w, int32_t h) {
    if (!argb_pixels) return;
    
    free(g_comp.wallpaper);
    g_comp.wallpaper = (uint32_t *)malloc(w * h * sizeof(uint32_t));
    memcpy(g_comp.wallpaper, argb_pixels, w * h * sizeof(uint32_t));
    g_comp.wallpaper_w = w;
    g_comp.wallpaper_h = h;
}

void comp_get_framebuffer(uint32_t **out_pixels, int32_t *out_w,
                         int32_t *out_h, int32_t *out_pitch) {
    *out_pixels = g_comp.framebuffer;
    *out_w = g_comp.screen_w;
    *out_h = g_comp.screen_h;
    *out_pitch = g_comp.screen_pitch;
}

void comp_composite(void) {
    /* 1. Clear to wallpaper */
    comp_tile_wallpaper();
    
    /* 2. Get window list from WM */
    wm_window_t *wins;
    int count;
    wm_get_window_list(&wins, &count);
    
    /* 3. Render each window in order (back to front) */
    for (int i = 0; i < count; i++) {
        wm_window_t *win = &wins[i];
        if (!(win->flags & WM_WIN_VISIBLE)) continue;
        
        /* Blur the frame region for Aero effect (title bar + borders) */
        if (win->flags & WM_WIN_DECORATED) {
            /* Blur only the border region (not the client area) */
            int margin = WM_BORDER_WIDTH + WM_TITLEBAR_HEIGHT;
            comp_blur_region(g_comp.framebuffer, g_comp.screen_w, g_comp.screen_h,
                           win->x, win->y, win->w, margin);
            if (win->h > margin) {
                comp_blur_region(g_comp.framebuffer, g_comp.screen_w, g_comp.screen_h,
                               win->x, win->y + win->h - WM_BORDER_WIDTH,
                               win->w, WM_BORDER_WIDTH);
            }
        }
        
        /* Draw title bar and borders */
        comp_draw_title_bar(win);
        comp_draw_borders(win);
        
        /* Blit the client area on top */
        comp_blit_window_client(win);
    }
}
