/* graphics_core.h - Framebuffer ownership and pixel-level primitives.
 *
 * This is the ONLY layer that touches g_framebuffer.framebuffer memory
 * directly. graphics_2d.c and graphics_3d.c are both built on top of
 * this and must not read/write framebuffer memory themselves - they
 * call into these primitives so bounds-checking, pitch/bpp handling,
 * and multiboot-tag validation stay in one audited place instead of
 * being re-derived (and potentially re-broken) in three files.
 */
#ifndef GRAPHICS_CORE_H
#define GRAPHICS_CORE_H

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

/* Color utilities.
 *
 * Casting each operand to (uint32_t) before shifting is required, not
 * decorative - r/g/b/a are uint8_t, which C promotes to (signed) int
 * before the shift. Shifting a value like 255 left by 24 sets the
 * sign bit of a 32-bit int, which is undefined behavior per the C
 * standard (verified directly: this exact call pattern reproducibly
 * trips UBSan's "left shift of 255 by 24 places cannot be represented
 * in type 'int'" at runtime). GCC happens to produce the intended bit
 * pattern in practice at the optimization levels this kernel builds
 * with, but relying on that is relying on undefined behavior
 * "working" rather than on well-defined semantics - a different
 * compiler, flag set, or future GCC version would be entitled to
 * produce something else. Shifting as uint32_t instead is well-defined
 * for every possible 8-bit input, with identical results on this
 * toolchain today. */
static inline color_t graphics_rgb(uint8_t r, uint8_t g, uint8_t b) {
    return 0xFF000000u
         | ((uint32_t)r << 16)
         | ((uint32_t)g << 8)
         | (uint32_t)b;
}

static inline color_t graphics_argb(uint8_t a, uint8_t r, uint8_t g, uint8_t b) {
    return ((uint32_t)a << 24)
         | ((uint32_t)r << 16)
         | ((uint32_t)g << 8)
         | (uint32_t)b;
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

/* ── Primitives added for the 2D/3D layers built on top of core ──────
 * These exist so graphics_2d.c (blitting, alpha blending, sprite
 * compositing) and graphics_3d.c (scanline triangle rasterization)
 * never need to touch g_framebuffer.framebuffer directly - every
 * memory access funnels through here, where bounds/pitch/bpp are
 * already handled once. */

/* Fill a horizontal run of pixels from x..x+len-1 on row y. Used by
 * the 3D rasterizer's scanline fill and by 2D's fast rect fill.
 * Clips exactly like graphics_fill_rect. */
void graphics_fill_span(uint32_t x, uint32_t y, uint32_t len, color_t color);

/* Read back a single pixel. Returns 0 (opaque black) if out of
 * bounds or graphics isn't ready, rather than reading undefined
 * memory - callers doing blending should treat that as "nothing
 * there" rather than trust it as real color data. */
color_t graphics_get_pixel(uint32_t x, uint32_t y);

/* Alpha-blended pixel write: blends `color`'s alpha channel against
 * whatever is already on screen at (x,y). This is the one primitive
 * that both 2D sprite/font alpha-blitting and 3D's (future) blended
 * transparency effects share, so the blend math is written once. */
void graphics_blend_pixel(uint32_t x, uint32_t y, color_t color);

/* Direct row pointer + pitch/bpp accessors, for the 3D rasterizer's
 * inner per-scanline loop where per-pixel function-call overhead
 * across a whole triangle fill would be a real cost. Still bounds-
 * checked at the call site (y must be < height) - this only skips
 * the *per-pixel* bounds check, not the row-level one. Returns NULL
 * if graphics isn't ready or y is out of range. */
uint8_t *graphics_get_row_ptr(uint32_t y);
uint32_t graphics_get_pitch(void);
uint32_t graphics_get_bpp(void);

/* Writes a raw pixel into a row pointer obtained from
 * graphics_get_row_ptr(), honoring current bpp. Caller guarantees x
 * is in-bounds (row pointer access is only safe within [0, width)). */
void graphics_put_pixel_raw(uint8_t *row, uint32_t x, color_t color);

#endif /* GRAPHICS_CORE_H */
