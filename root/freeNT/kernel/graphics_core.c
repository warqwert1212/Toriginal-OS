/* Graphics framework for freeNT - VESA/Framebuffer support
 *
 * Backs the interface declared in graphics.h with a real linear
 * framebuffer, sourced from the multiboot2 framebuffer info tag
 * (see boot64.s's request tag + kernel.c's parse_multiboot()).
 *
 * All draw calls are bounds-checked against g_framebuffer.width/height
 * so a bad x/y from a caller can't walk off the end of framebuffer
 * memory into unrelated physical pages.
 */

#include "graphics_core.h"
#include "kernel.h"
#include "heap.h"
#include "string.h"

framebuffer_t g_framebuffer;

static int g_gfx_ready = 0;
/* The real, hardware-scanned-out framebuffer address - kept separate
 * from g_framebuffer.framebuffer once a back buffer is allocated (see
 * graphics_init()), since every existing draw primitive in this file
 * reads/writes g_framebuffer.framebuffer directly and none of them
 * needed to change for double buffering to work - they just end up
 * drawing to the back buffer instead of the screen, invisibly. */
static uint8_t *g_hw_framebuffer = NULL;

int graphics_is_available(void) {
    return g_gfx_ready;
}

int graphics_init(void) {
    if (!mb_fb_found()) {
        g_gfx_ready = 0;
        return -1;
    }

    uint64_t addr  = mb_fb_addr();
    uint32_t pitch = mb_fb_pitch();
    uint32_t width = mb_fb_width();
    uint32_t height = mb_fb_height();
    uint8_t  bpp   = mb_fb_bpp();

    /* Sanity-check what GRUB reported before trusting it. A zero
     * width/height/pitch or an address that doesn't fit in a 32-bit
     * pointer (we're not mapping high physical memory for the
     * framebuffer in this stage) means "don't trust this tag" rather
     * than silently computing garbage offsets later. */
    if (width == 0 || height == 0 || pitch == 0) {
        g_gfx_ready = 0;
        return -1;
    }
    if (addr > 0xFFFFFFFFULL) {
        /* Framebuffer physical address is above 4GiB - freeNT's
         * current memory model (see mm.c) doesn't identity-map high
         * physical memory, so we can't safely dereference this
         * without more paging work. Bail out to VGA text mode rather
         * than fault. */
        g_gfx_ready = 0;
        return -1;
    }

    g_framebuffer.width  = width;
    g_framebuffer.height = height;
    g_framebuffer.depth  = bpp;
    g_framebuffer.pitch  = pitch;
    g_hw_framebuffer = (uint8_t *)(uintptr_t)addr;
    g_framebuffer.mode = (bpp == 16) ? GRAPHICS_MODE_VESA_16BIT
                                     : GRAPHICS_MODE_VESA_32BIT;

    /* Back buffer: every draw call writes here, not to the real
     * hardware framebuffer - graphics_present() (called once per
     * frame by desktop.c) is the only thing that ever touches
     * g_hw_framebuffer, in one bulk copy. Without this, the flicker
     * seen in windows/cursor was every single fill_rect/blit call
     * being visible on screen the instant it ran, so a frame could be
     * scanned out to the display half-drawn. */
    size_t fb_bytes = (size_t)pitch * height;
    uint8_t *shadow = (uint8_t *)kmalloc(fb_bytes);
    if (shadow) {
        memset(shadow, 0, fb_bytes);
        g_framebuffer.framebuffer = shadow;
    } else {
        /* Couldn't get a back buffer - draw straight to hardware
         * instead of refusing to boot graphics at all. Flickery, but
         * a working flickery desktop beats none. */
        g_framebuffer.framebuffer = g_hw_framebuffer;
    }

    g_gfx_ready = 1;
    return 0;
}

void graphics_present(void) {
    if (!g_gfx_ready || !g_hw_framebuffer) return;
    if (g_framebuffer.framebuffer == g_hw_framebuffer) return; /* no back buffer active - already drawing direct */
    size_t fb_bytes = (size_t)g_framebuffer.pitch * g_framebuffer.height;
    memcpy(g_hw_framebuffer, g_framebuffer.framebuffer, fb_bytes);
}

int graphics_set_mode(graphics_mode_t mode, uint32_t width, uint32_t height) {
    /* Mode switching after boot would require re-invoking VBE, which
     * needs real-mode (or a v86 monitor) - out of scope for this
     * stage. The mode is fixed at whatever GRUB negotiated via the
     * multiboot framebuffer request tag. Report not-supported rather
     * than pretending to succeed. */
    (void)mode; (void)width; (void)height;
    return -1;
}

/* Public row-pointer + bpp-switched pixel write. This is the single
 * place that knows how to pack a color_t into 32/24/16-bit pixel
 * formats - every other write path (checked, span, blend) funnels
 * through this so a future pixel-format fix only needs to happen
 * once. Caller guarantees `row` came from graphics_get_row_ptr() and
 * that x < g_framebuffer.width. */
void graphics_put_pixel_raw(uint8_t *row, uint32_t x, color_t color) {
    if (g_framebuffer.depth == 32) {
        uint32_t *px = (uint32_t *)(row + (uint64_t)x * 4);
        *px = color;
    } else if (g_framebuffer.depth == 24) {
        uint8_t *px = row + (uint64_t)x * 3;
        px[0] = (uint8_t)(color & 0xFF);
        px[1] = (uint8_t)((color >> 8) & 0xFF);
        px[2] = (uint8_t)((color >> 16) & 0xFF);
    } else if (g_framebuffer.depth == 16) {
        /* 16-bit path kept for completeness even though graphics_init()
         * currently only accepts bpp>=24 from the multiboot tag - if
         * that constraint ever loosens this path is ready. RGB565. */
        uint16_t *px = (uint16_t *)(row + (uint64_t)x * 2);
        uint8_t r = (uint8_t)((color >> 16) & 0xFF);
        uint8_t g = (uint8_t)((color >> 8) & 0xFF);
        uint8_t b = (uint8_t)(color & 0xFF);
        *px = (uint16_t)(((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3));
    }
}

uint8_t *graphics_get_row_ptr(uint32_t y) {
    if (!g_gfx_ready || y >= g_framebuffer.height) return (uint8_t *)0;
    return g_framebuffer.framebuffer + (uint64_t)y * g_framebuffer.pitch;
}

uint32_t graphics_get_pitch(void) { return g_framebuffer.pitch; }
uint32_t graphics_get_bpp(void)   { return g_framebuffer.depth; }

/* Internal: write one pixel with no bounds check. Only call this
 * after the caller has already validated x < width && y < height.
 * Kept as the hot path for the existing draw_pixel/rect/line/circle
 * routines below - now just a thin wrapper over the public row-ptr
 * API so there's exactly one bpp-switch implementation in the file. */
static inline void put_pixel_unchecked(uint32_t x, uint32_t y, color_t color) {
    uint8_t *row = g_framebuffer.framebuffer + (uint64_t)y * g_framebuffer.pitch;
    graphics_put_pixel_raw(row, x, color);
}

void graphics_draw_pixel(uint32_t x, uint32_t y, color_t color) {
    if (!g_gfx_ready) return;
    if (x >= g_framebuffer.width || y >= g_framebuffer.height) return;
    put_pixel_unchecked(x, y, color);
}

/* Read back whatever's currently at (x,y), unpacked into the same
 * ARGB color_t format used everywhere else, regardless of the
 * framebuffer's actual bpp. Used by graphics_blend_pixel() and by
 * the 2D layer's alpha compositing. */
color_t graphics_get_pixel(uint32_t x, uint32_t y) {
    if (!g_gfx_ready) return 0;
    if (x >= g_framebuffer.width || y >= g_framebuffer.height) return 0;

    uint8_t *row = g_framebuffer.framebuffer + (uint64_t)y * g_framebuffer.pitch;

    if (g_framebuffer.depth == 32) {
        uint32_t *px = (uint32_t *)(row + (uint64_t)x * 4);
        return *px;
    } else if (g_framebuffer.depth == 24) {
        uint8_t *px = row + (uint64_t)x * 3;
        return graphics_rgb(px[2], px[1], px[0]);
    } else if (g_framebuffer.depth == 16) {
        uint16_t *px = (uint16_t *)(row + (uint64_t)x * 2);
        uint16_t v = *px;
        uint8_t r = (uint8_t)((v >> 8) & 0xF8);
        uint8_t g = (uint8_t)((v >> 3) & 0xFC);
        uint8_t b = (uint8_t)((v << 3) & 0xF8);
        return graphics_rgb(r, g, b);
    }
    return 0;
}

/* Alpha-blend `color` over whatever's already at (x,y). Straight
 * "over" compositing: out = src*srcA + dst*(1-srcA), done per
 * channel in integer math (no float unit assumed available this
 * early in kernel boot). Fully opaque (alpha==255) and fully
 * transparent (alpha==0) short-circuit to avoid the blend math and
 * the extra get_pixel read entirely - both are extremely common
 * cases (font glyph interiors, sprite backgrounds). */
void graphics_blend_pixel(uint32_t x, uint32_t y, color_t color) {
    if (!g_gfx_ready) return;
    if (x >= g_framebuffer.width || y >= g_framebuffer.height) return;

    uint8_t src_a = (uint8_t)((color >> 24) & 0xFF);
    if (src_a == 0) return;                    /* fully transparent: no-op */
    if (src_a == 255) {                        /* fully opaque: plain write */
        put_pixel_unchecked(x, y, color);
        return;
    }

    color_t dst = graphics_get_pixel(x, y);
    uint8_t src_r = (uint8_t)((color >> 16) & 0xFF);
    uint8_t src_g = (uint8_t)((color >> 8) & 0xFF);
    uint8_t src_b = (uint8_t)(color & 0xFF);
    uint8_t dst_r = (uint8_t)((dst >> 16) & 0xFF);
    uint8_t dst_g = (uint8_t)((dst >> 8) & 0xFF);
    uint8_t dst_b = (uint8_t)(dst & 0xFF);

    uint8_t inv_a = (uint8_t)(255 - src_a);
    uint8_t out_r = (uint8_t)(((uint32_t)src_r * src_a + (uint32_t)dst_r * inv_a) / 255);
    uint8_t out_g = (uint8_t)(((uint32_t)src_g * src_a + (uint32_t)dst_g * inv_a) / 255);
    uint8_t out_b = (uint8_t)(((uint32_t)src_b * src_a + (uint32_t)dst_b * inv_a) / 255);

    put_pixel_unchecked(x, y, graphics_rgb(out_r, out_g, out_b));
}

/* Fill a horizontal run - the workhorse both the 2D fast-fill path
 * and the 3D scanline rasterizer use, so it's written once here with
 * proper clipping rather than duplicated as a per-pixel loop in two
 * other files. */
void graphics_fill_span(uint32_t x, uint32_t y, uint32_t len, color_t color) {
    if (!g_gfx_ready) return;
    if (y >= g_framebuffer.height || x >= g_framebuffer.width || len == 0) return;

    uint32_t x_end = x + len;
    if (x_end > g_framebuffer.width) x_end = g_framebuffer.width;

    uint8_t *row = g_framebuffer.framebuffer + (uint64_t)y * g_framebuffer.pitch;
    for (uint32_t xx = x; xx < x_end; xx++) {
        graphics_put_pixel_raw(row, xx, color);
    }
}

void graphics_clear_screen(color_t color) {
    if (!g_gfx_ready) return;
    for (uint32_t y = 0; y < g_framebuffer.height; y++) {
        for (uint32_t x = 0; x < g_framebuffer.width; x++) {
            put_pixel_unchecked(x, y, color);
        }
    }
}

void graphics_fill_rect(uint32_t x, uint32_t y, uint32_t width, uint32_t height, color_t color) {
    if (!g_gfx_ready) return;

    /* Clip rather than reject: a rect that starts on-screen but runs
     * past the edge should still draw its visible portion, matching
     * how every other framebuffer-drawing API behaves. */
    if (x >= g_framebuffer.width || y >= g_framebuffer.height) return;

    uint32_t x_end = x + width;
    uint32_t y_end = y + height;
    if (x_end > g_framebuffer.width)  x_end = g_framebuffer.width;
    if (y_end > g_framebuffer.height) y_end = g_framebuffer.height;

    for (uint32_t yy = y; yy < y_end; yy++) {
        for (uint32_t xx = x; xx < x_end; xx++) {
            put_pixel_unchecked(xx, yy, color);
        }
    }
}

void graphics_draw_rect(uint32_t x, uint32_t y, uint32_t width, uint32_t height, color_t color) {
    if (!g_gfx_ready || width == 0 || height == 0) return;

    /* Outline only: top, bottom, left, right edges. Each edge call
     * goes through graphics_fill_rect so clipping is handled once,
     * in one place, instead of duplicated per edge. */
    graphics_fill_rect(x, y, width, 1, color);                       /* top    */
    graphics_fill_rect(x, y + height - 1, width, 1, color);          /* bottom */
    graphics_fill_rect(x, y, 1, height, color);                      /* left   */
    graphics_fill_rect(x + width - 1, y, 1, height, color);          /* right  */
}

void graphics_draw_line(uint32_t x1, uint32_t y1, uint32_t x2, uint32_t y2, color_t color) {
    if (!g_gfx_ready) return;

    /* Bresenham's line algorithm, done in signed space since the
     * midpoint accumulator can legitimately go negative even though
     * the public API takes unsigned coordinates. */
    int32_t sx1 = (int32_t)x1, sy1 = (int32_t)y1;
    int32_t sx2 = (int32_t)x2, sy2 = (int32_t)y2;

    int32_t dx = sx2 - sx1; if (dx < 0) dx = -dx;
    int32_t dy = sy2 - sy1; if (dy < 0) dy = -dy;
    int32_t sx = (sx1 < sx2) ? 1 : -1;
    int32_t sy = (sy1 < sy2) ? 1 : -1;
    int32_t err = dx - dy;

    /* Cap iterations defensively so a corrupted/garbage coordinate
     * pair can't spin forever - line length can't exceed the sum of
     * framebuffer width+height in any real draw call. */
    uint32_t max_iter = g_framebuffer.width + g_framebuffer.height + 8;

    for (uint32_t i = 0; i < max_iter; i++) {
        if (sx1 >= 0 && sy1 >= 0 &&
            (uint32_t)sx1 < g_framebuffer.width &&
            (uint32_t)sy1 < g_framebuffer.height) {
            put_pixel_unchecked((uint32_t)sx1, (uint32_t)sy1, color);
        }

        if (sx1 == sx2 && sy1 == sy2) break;

        int32_t e2 = 2 * err;
        if (e2 > -dy) { err -= dy; sx1 += sx; }
        if (e2 <  dx) { err += dx; sy1 += sy; }
    }
}

void graphics_draw_circle(uint32_t x, uint32_t y, uint32_t radius, color_t color) {
    if (!g_gfx_ready) return;

    /* Midpoint circle algorithm, 8-way symmetry. Center coords kept
     * signed since points can legitimately fall off-screen (clipped
     * per-point below) when the circle is near an edge. */
    int32_t cx = (int32_t)x, cy = (int32_t)y;
    int32_t px = (int32_t)radius, py = 0;
    int32_t err = 1 - px;

    #define PLOT8(dx, dy) do { \
        int32_t xs[4] = { cx+(dx), cx-(dx), cx+(dx), cx-(dx) }; \
        int32_t ys[4] = { cy+(dy), cy+(dy), cy-(dy), cy-(dy) }; \
        for (int _i = 0; _i < 4; _i++) { \
            if (xs[_i] >= 0 && ys[_i] >= 0 && \
                (uint32_t)xs[_i] < g_framebuffer.width && \
                (uint32_t)ys[_i] < g_framebuffer.height) { \
                put_pixel_unchecked((uint32_t)xs[_i], (uint32_t)ys[_i], color); \
            } \
        } \
    } while (0)

    while (px >= py) {
        PLOT8(px, py);
        PLOT8(py, px);
        py++;
        if (err < 0) {
            err += 2 * py + 1;
        } else {
            px--;
            err += 2 * (py - px) + 1;
        }
    }

    #undef PLOT8
}
