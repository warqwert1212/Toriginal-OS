/* graphics_2d.c - 2D compositing layer built on graphics_core.
 *
 * Every screen-touching function here calls into graphics_core's
 * put/blend pixel or fill_span - this file never indexes
 * g_framebuffer.framebuffer directly.
 */

#include "graphics_2d.h"
#include "heap.h"

/* ── Clip stack ───────────────────────────────────────────────────── */

#define CLIP_STACK_MAX 32

static gfx2d_rect_t clip_stack[CLIP_STACK_MAX];
static int clip_stack_depth = 0;

static gfx2d_rect_t full_screen_rect(void) {
    gfx2d_rect_t r;
    r.x = 0;
    r.y = 0;
    r.w = g_framebuffer.width;
    r.h = g_framebuffer.height;
    return r;
}

int gfx2d_rect_intersect(gfx2d_rect_t a, gfx2d_rect_t b, gfx2d_rect_t *out) {
    int32_t ax2 = a.x + (int32_t)a.w;
    int32_t ay2 = a.y + (int32_t)a.h;
    int32_t bx2 = b.x + (int32_t)b.w;
    int32_t by2 = b.y + (int32_t)b.h;

    int32_t x1 = (a.x > b.x) ? a.x : b.x;
    int32_t y1 = (a.y > b.y) ? a.y : b.y;
    int32_t x2 = (ax2 < bx2) ? ax2 : bx2;
    int32_t y2 = (ay2 < by2) ? ay2 : by2;

    if (x2 <= x1 || y2 <= y1) {
        /* No overlap - report an empty rect rather than leaving
         * *out uninitialized, so callers can't accidentally read
         * garbage bounds after a failed intersection. */
        out->x = 0; out->y = 0; out->w = 0; out->h = 0;
        return 0;
    }

    out->x = x1;
    out->y = y1;
    out->w = (uint32_t)(x2 - x1);
    out->h = (uint32_t)(y2 - y1);
    return 1;
}

int gfx2d_rect_contains_point(gfx2d_rect_t r, int32_t x, int32_t y) {
    return x >= r.x && y >= r.y &&
           x < r.x + (int32_t)r.w && y < r.y + (int32_t)r.h;
}

void gfx2d_clip_push(gfx2d_rect_t rect) {
    gfx2d_rect_t current = gfx2d_clip_current();
    gfx2d_rect_t intersected;
    gfx2d_rect_intersect(current, rect, &intersected);

    if (clip_stack_depth >= CLIP_STACK_MAX) {
        /* Stack exhausted - rather than corrupt memory past the
         * array or silently drop the clip entirely (which would let
         * drawing escape its intended bounds), clamp: overwrite the
         * deepest existing entry with the new intersection so
         * clipping only ever gets *tighter*, never disappears. */
        clip_stack[CLIP_STACK_MAX - 1] = intersected;
        return;
    }

    clip_stack[clip_stack_depth] = intersected;
    clip_stack_depth++;
}

void gfx2d_clip_pop(void) {
    if (clip_stack_depth > 0) {
        clip_stack_depth--;
    }
}

gfx2d_rect_t gfx2d_clip_current(void) {
    if (clip_stack_depth == 0) {
        return full_screen_rect();
    }
    return clip_stack[clip_stack_depth - 1];
}

/* ── Surface lifecycle ────────────────────────────────────────────── */

gfx2d_surface_t gfx2d_surface_create(uint32_t width, uint32_t height) {
    gfx2d_surface_t surf;
    surf.width = 0;
    surf.height = 0;
    surf.pixels = (color_t *)0;

    if (width == 0 || height == 0) return surf;

    /* Overflow guard: width*height*4 must not wrap a size_t before
     * being handed to kmalloc. Reject rather than let a wrapped
     * (tiny) allocation succeed and then get treated as a full
     * width*height buffer by every other gfx2d_surface_* call. */
    uint64_t pixel_count = (uint64_t)width * (uint64_t)height;
    uint64_t byte_count = pixel_count * sizeof(color_t);
    if (byte_count > 0x10000000ULL /* 256MiB sanity ceiling */) {
        return surf;
    }

    void *mem = kmalloc((size_t)byte_count);
    if (!mem) return surf;

    surf.width = width;
    surf.height = height;
    surf.pixels = (color_t *)mem;
    return surf;
}

void gfx2d_surface_destroy(gfx2d_surface_t *surf) {
    if (!surf || !surf->pixels) return;
    kfree(surf->pixels);
    surf->pixels = (color_t *)0;
    surf->width = 0;
    surf->height = 0;
}

void gfx2d_surface_clear(gfx2d_surface_t *surf, color_t color) {
    if (!surf || !surf->pixels) return;
    uint64_t n = (uint64_t)surf->width * surf->height;
    for (uint64_t i = 0; i < n; i++) surf->pixels[i] = color;
}

void gfx2d_surface_set_pixel(gfx2d_surface_t *surf, uint32_t x, uint32_t y, color_t color) {
    if (!surf || !surf->pixels) return;
    if (x >= surf->width || y >= surf->height) return;
    surf->pixels[(uint64_t)y * surf->width + x] = color;
}

color_t gfx2d_surface_get_pixel(const gfx2d_surface_t *surf, uint32_t x, uint32_t y) {
    if (!surf || !surf->pixels) return 0;
    if (x >= surf->width || y >= surf->height) return 0;
    return surf->pixels[(uint64_t)y * surf->width + x];
}

/* ── Blitting ─────────────────────────────────────────────────────── */

/* Shared blit core: iterates the overlap between (surface content at
 * dst_x,dst_y) and the current clip rect, writing each pixel via
 * either a plain write (use_alpha==0) or graphics_blend_pixel
 * (use_alpha==1). src_rect selects which part of the surface to read
 * from, in surface-local coordinates - callers passing the full
 * surface bounds get ordinary whole-surface blitting. */
static void blit_core(const gfx2d_surface_t *surf, gfx2d_rect_t src_rect,
                      int32_t dst_x, int32_t dst_y, int use_alpha) {
    if (!surf || !surf->pixels) return;

    gfx2d_rect_t clip = gfx2d_clip_current();

    /* Destination rect on screen this blit would occupy, before
     * clipping. */
    gfx2d_rect_t dst_rect;
    dst_rect.x = dst_x;
    dst_rect.y = dst_y;
    dst_rect.w = src_rect.w;
    dst_rect.h = src_rect.h;

    gfx2d_rect_t visible;
    if (!gfx2d_rect_intersect(dst_rect, clip, &visible)) return;

    for (uint32_t row = 0; row < visible.h; row++) {
        int32_t screen_y = visible.y + (int32_t)row;
        int32_t src_y = (screen_y - dst_y) + src_rect.y;
        if (src_y < 0 || (uint32_t)src_y >= surf->height) continue;

        for (uint32_t col = 0; col < visible.w; col++) {
            int32_t screen_x = visible.x + (int32_t)col;
            int32_t src_x = (screen_x - dst_x) + src_rect.x;
            if (src_x < 0 || (uint32_t)src_x >= surf->width) continue;

            color_t px = surf->pixels[(uint64_t)src_y * surf->width + (uint32_t)src_x];

            if (use_alpha) {
                graphics_blend_pixel((uint32_t)screen_x, (uint32_t)screen_y, px);
            } else {
                graphics_draw_pixel((uint32_t)screen_x, (uint32_t)screen_y, px);
            }
        }
    }
}

void gfx2d_blit(const gfx2d_surface_t *surf, int32_t dst_x, int32_t dst_y) {
    if (!surf) return;
    gfx2d_rect_t full = { 0, 0, surf->width, surf->height };
    blit_core(surf, full, dst_x, dst_y, 0);
}

void gfx2d_blit_alpha(const gfx2d_surface_t *surf, int32_t dst_x, int32_t dst_y) {
    if (!surf) return;
    gfx2d_rect_t full = { 0, 0, surf->width, surf->height };
    blit_core(surf, full, dst_x, dst_y, 1);
}

void gfx2d_blit_region(const gfx2d_surface_t *surf, gfx2d_rect_t src_rect,
                       int32_t dst_x, int32_t dst_y, int use_alpha) {
    blit_core(surf, src_rect, dst_x, dst_y, use_alpha);
}

/* ── Clip-aware shapes ────────────────────────────────────────────── */

void gfx2d_fill_rect(gfx2d_rect_t rect, color_t color) {
    gfx2d_rect_t clip = gfx2d_clip_current();
    gfx2d_rect_t visible;
    if (!gfx2d_rect_intersect(rect, clip, &visible)) return;

    for (uint32_t row = 0; row < visible.h; row++) {
        graphics_fill_span((uint32_t)visible.x, (uint32_t)(visible.y + (int32_t)row),
                           visible.w, color);
    }
}

void gfx2d_draw_rect(gfx2d_rect_t rect, color_t color) {
    if (rect.w == 0 || rect.h == 0) return;

    /* Guard against rect.x/y + width/height overflowing int32_t.
     * Not reachable from any current caller (every gfx2d_draw_rect
     * call in this kernel uses screen-space coordinates, max ~1024),
     * but this function takes arbitrary caller-supplied rects, and
     * every other bounds-sensitive function in this file (blit_core,
     * gfx2d_fill_rect via gfx2d_rect_intersect) already defends
     * against pathological input - leaving this one function as the
     * exception would be an inconsistent, easy-to-forget gap once
     * something else in the kernel starts constructing rects from
     * computed rather than literal values. */
    int64_t x_end = (int64_t)rect.x + (int64_t)rect.w;
    int64_t y_end = (int64_t)rect.y + (int64_t)rect.h;
    if (x_end > 0x7FFFFFFFLL || y_end > 0x7FFFFFFFLL) return;

    gfx2d_rect_t top    = { rect.x, rect.y, rect.w, 1 };
    gfx2d_rect_t bottom = { rect.x, rect.y + (int32_t)rect.h - 1, rect.w, 1 };
    gfx2d_rect_t left   = { rect.x, rect.y, 1, rect.h };
    gfx2d_rect_t right  = { rect.x + (int32_t)rect.w - 1, rect.y, 1, rect.h };

    gfx2d_fill_rect(top, color);
    gfx2d_fill_rect(bottom, color);
    gfx2d_fill_rect(left, color);
    gfx2d_fill_rect(right, color);
}

void gfx2d_draw_line(int32_t x1, int32_t y1, int32_t x2, int32_t y2, color_t color) {
    /* Clip-aware line: clamp both endpoints against the current clip
     * rect using Cohen-Sutherland-lite logic would be ideal, but for
     * the cases this kernel actually needs (window borders, cursor
     * hotspot lines, simple UI chrome) a per-pixel clip-test inside
     * the same Bresenham walk graphics_core uses is simpler and
     * plenty fast at these resolutions. */
    gfx2d_rect_t clip = gfx2d_clip_current();

    int32_t dx = x2 - x1; if (dx < 0) dx = -dx;
    int32_t dy = y2 - y1; if (dy < 0) dy = -dy;
    int32_t sx = (x1 < x2) ? 1 : -1;
    int32_t sy = (y1 < y2) ? 1 : -1;
    int32_t err = dx - dy;

    uint32_t max_iter = g_framebuffer.width + g_framebuffer.height + 8;

    for (uint32_t i = 0; i < max_iter; i++) {
        if (gfx2d_rect_contains_point(clip, x1, y1)) {
            graphics_draw_pixel((uint32_t)x1, (uint32_t)y1, color);
        }

        if (x1 == x2 && y1 == y2) break;

        int32_t e2 = 2 * err;
        if (e2 > -dy) { err -= dy; x1 += sx; }
        if (e2 <  dx) { err += dx; y1 += sy; }
    }
}
