/* graphics_2d.h - 2D compositing layer built on graphics_core.
 *
 * This is what the GUI/WM, terminal, and font renderer are meant to
 * call. Nothing in here touches g_framebuffer memory directly - every
 * pixel write goes through graphics_core's put_pixel/blend_pixel/
 * fill_span/get_row_ptr primitives, so bounds-safety only has to be
 * gotten right once.
 *
 * Concepts:
 *   - gfx2d_surface_t: an off-screen pixel buffer (used for sprites,
 *     window contents, cursor images, glyph bitmaps at render time).
 *     Surfaces are plain heap-backed ARGB buffers - blitting one to
 *     the screen (or to another surface) is how compositing happens.
 *   - gfx2d_rect_t / clip stack: bounds a drawing operation so a
 *     window's contents can never be blitted outside its own frame,
 *     which the future WM will rely on for overlapping windows.
 */
#ifndef GRAPHICS_2D_H
#define GRAPHICS_2D_H

#include "graphics_core.h"

typedef struct {
    int32_t x, y;
    uint32_t w, h;
} gfx2d_rect_t;

/* Off-screen ARGB surface. `pixels` is always width*height uint32_t
 * (32bpp internally regardless of the real framebuffer's depth -
 * graphics_core's put/blend pixel handle the final format conversion
 * when a surface is blitted to screen). */
typedef struct {
    uint32_t width;
    uint32_t height;
    color_t *pixels;
} gfx2d_surface_t;

/* ── Surface lifecycle ────────────────────────────────────────────── */
/* Allocates via kmalloc (see heap.c) - returns w=h=0/pixels=NULL on
 * allocation failure so callers can check `.pixels != NULL` rather
 * than crash on an unchecked NULL surface later. */
gfx2d_surface_t gfx2d_surface_create(uint32_t width, uint32_t height);
void gfx2d_surface_destroy(gfx2d_surface_t *surf);
void gfx2d_surface_clear(gfx2d_surface_t *surf, color_t color);
void gfx2d_surface_set_pixel(gfx2d_surface_t *surf, uint32_t x, uint32_t y, color_t color);
color_t gfx2d_surface_get_pixel(const gfx2d_surface_t *surf, uint32_t x, uint32_t y);

/* ── Clip stack ───────────────────────────────────────────────────── */
/* Pushes an intersection of the current clip with `rect` - all screen
 * writes below this point are confined to the intersection until
 * gfx2d_clip_pop(). Depth is bounded (see .c for the constant) so a
 * runaway push loop can't overflow a fixed-size stack; push silently
 * clamps to the deepest existing rect once the stack is full rather
 * than doing unbounded allocation. */
void gfx2d_clip_push(gfx2d_rect_t rect);
void gfx2d_clip_pop(void);
gfx2d_rect_t gfx2d_clip_current(void);

/* ── Blitting: surface -> screen ──────────────────────────────────── */
/* Opaque blit - ignores per-pixel alpha, straight copy. Fast path for
 * fully-opaque content like solid window backgrounds. */
void gfx2d_blit(const gfx2d_surface_t *surf, int32_t dst_x, int32_t dst_y);

/* Alpha-composited blit - every pixel goes through
 * graphics_blend_pixel() so semi-transparent sprites (cursor
 * shadows, translucent window chrome) composite correctly. Slower
 * than gfx2d_blit(); use only where alpha actually varies. */
void gfx2d_blit_alpha(const gfx2d_surface_t *surf, int32_t dst_x, int32_t dst_y);

/* Blit a sub-rectangle of `surf` (src_rect, in surface-local coords)
 * to screen at (dst_x, dst_y). Used for sprite sheets / font atlases
 * where many glyphs live in one surface. */
void gfx2d_blit_region(const gfx2d_surface_t *surf, gfx2d_rect_t src_rect,
                       int32_t dst_x, int32_t dst_y, int use_alpha);

/* ── Shapes (clip-aware wrappers over graphics_core) ────────────────
 * These exist distinctly from graphics_core's shape primitives
 * because they respect the current clip rect - core's primitives
 * only clip to the framebuffer edge, these also clip to whatever
 * gfx2d_clip_push() last established (i.e. a window's own bounds). */
void gfx2d_fill_rect(gfx2d_rect_t rect, color_t color);
void gfx2d_draw_rect(gfx2d_rect_t rect, color_t color);
void gfx2d_draw_line(int32_t x1, int32_t y1, int32_t x2, int32_t y2, color_t color);

/* ── Rect utilities ───────────────────────────────────────────────── */
int gfx2d_rect_intersect(gfx2d_rect_t a, gfx2d_rect_t b, gfx2d_rect_t *out);
int gfx2d_rect_contains_point(gfx2d_rect_t r, int32_t x, int32_t y);

#endif /* GRAPHICS_2D_H */
