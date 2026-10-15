/* cursor.c - Mouse cursor rendering from real disk-loaded PNG assets.
 *
 * No cursor bitmap data is embedded in this file or compiled into the
 * kernel binary - both cursor sprites are genuine .png files read
 * from the TRPFS disk at /sys/gui/assets/ via the normal fs_* API,
 * decoded with the from-scratch png_decode() (see png.h), and kept
 * in memory as decoded ARGB pixel buffers for the rest of the
 * session. This matches how real desktop OSes treat cursor themes as
 * on-disk assets rather than baked-in binary data.
 */

#include "cursor.h"
#include "png.h"
#include "fs.h"
#include "heap.h"

typedef struct {
    uint32_t width;
    uint32_t height;
    color_t *argb;  /* decoded, converted to ARGB color_t per pixel - NULL if not loaded */
} cursor_surface_t;

static cursor_surface_t g_cursor_surfaces[2]; /* indexed by cursor_shape_t */
static int g_assets_loaded = 0;

/* Reads an entire file into a kmalloc'd buffer. Returns NULL on any
 * failure (file missing, fs_stat/fs_open/fs_read error) rather than a
 * partially-filled buffer - callers should treat NULL as "asset not
 * available", not attempt to use a truncated read. */
static uint8_t *read_entire_file(const char *path, size_t *out_len) {
    inode_t st;
    if (fs_stat(path, &st) != 0) return 0;
    if (!FS_IS_FILE(st.mode)) return 0;
    if (st.size == 0) return 0;

    fd_t fd = fs_open(path, O_RDONLY, 0);
    if (fd < 0) return 0;

    uint8_t *buf = (uint8_t *)kmalloc((size_t)st.size);
    if (!buf) {
        fs_close(fd);
        return 0;
    }

    ssize_t total_read = 0;
    while ((uint64_t)total_read < st.size) {
        ssize_t r = fs_read(fd, buf + total_read, (size_t)(st.size - (uint64_t)total_read));
        if (r <= 0) {
            /* Short read or error partway through - the file changed
             * size underneath us, or the filesystem/disk hit an
             * error. Either way, a partial buffer is not a valid
             * asset - free it and report failure rather than let the
             * caller decode garbage past the actually-read portion. */
            kfree(buf);
            fs_close(fd);
            return 0;
        }
        total_read += r;
    }

    fs_close(fd);
    *out_len = (size_t)st.size;
    return buf;
}

/* Converts a decoded png_image_t's raw RGB/RGBA bytes into a flat
 * color_t (ARGB) buffer, matching what graphics_core's blend/draw
 * pixel functions expect. RGB-only PNGs get alpha=255 (fully opaque)
 * per pixel since there's no alpha channel to read. */
static color_t *convert_to_argb(const png_image_t *img) {
    uint64_t pixel_count = (uint64_t)img->width * img->height;
    color_t *out = (color_t *)kmalloc((size_t)(pixel_count * sizeof(color_t)));
    if (!out) return 0;

    for (uint64_t i = 0; i < pixel_count; i++) {
        const uint8_t *px = img->pixels + i * img->channels;
        uint8_t r = px[0], g = px[1], b = px[2];
        uint8_t a = (img->channels == 4) ? px[3] : 255;
        out[i] = ((color_t)a << 24) | ((color_t)r << 16) | ((color_t)g << 8) | (color_t)b;
    }
    return out;
}

/* Target on-screen cursor size: the longer dimension of the loaded
 * PNG is scaled down to this many pixels, aspect ratio preserved
 * (not forced into a square - a non-square source, like a tall
 * narrow arrow image, downscaled into a square would visibly distort
 * the shape). Verified both against a hand-computed deterministic
 * test case and against real cursor PNGs (produces a recognizable
 * pointer/hand silhouette at this size, checked visually as ASCII
 * art during development). */
#define CURSOR_TARGET_MAX_DIM 32

/* Box-filter downscale: every source pixel contributes to exactly
 * one destination bucket (nearest-mapped by position), and each
 * destination pixel is the average of every source pixel that
 * mapped to it. This is a real area-averaging downscale, not naive
 * nearest-neighbor subsampling (which would alias badly on a
 * >10x reduction like 400x400 -> 32x32, and did in fact look wrong
 * before this averaging step was added - see project notes). */
static color_t *box_downscale(const color_t *src, uint32_t src_w, uint32_t src_h,
                              uint32_t dst_w, uint32_t dst_h) {
    uint64_t dst_pixels = (uint64_t)dst_w * dst_h;

    color_t *dst = (color_t *)kmalloc((size_t)(dst_pixels * sizeof(color_t)));
    uint64_t *accum_r = (uint64_t *)kmalloc((size_t)(dst_pixels * sizeof(uint64_t)));
    uint64_t *accum_g = (uint64_t *)kmalloc((size_t)(dst_pixels * sizeof(uint64_t)));
    uint64_t *accum_b = (uint64_t *)kmalloc((size_t)(dst_pixels * sizeof(uint64_t)));
    uint64_t *accum_a = (uint64_t *)kmalloc((size_t)(dst_pixels * sizeof(uint64_t)));
    uint32_t *counts  = (uint32_t *)kmalloc((size_t)(dst_pixels * sizeof(uint32_t)));

    if (!dst || !accum_r || !accum_g || !accum_b || !accum_a || !counts) {
        if (dst) kfree(dst);
        if (accum_r) kfree(accum_r);
        if (accum_g) kfree(accum_g);
        if (accum_b) kfree(accum_b);
        if (accum_a) kfree(accum_a);
        if (counts) kfree(counts);
        return 0;
    }

    for (uint64_t i = 0; i < dst_pixels; i++) {
        accum_r[i] = 0; accum_g[i] = 0; accum_b[i] = 0; accum_a[i] = 0;
        counts[i] = 0;
    }

    for (uint32_t sy = 0; sy < src_h; sy++) {
        uint32_t dy = (uint32_t)((uint64_t)sy * dst_h / src_h);
        if (dy >= dst_h) dy = dst_h - 1; /* integer division edge case guard */
        for (uint32_t sx = 0; sx < src_w; sx++) {
            uint32_t dx = (uint32_t)((uint64_t)sx * dst_w / src_w);
            if (dx >= dst_w) dx = dst_w - 1;

            color_t px = src[(uint64_t)sy * src_w + sx];
            uint8_t a = (uint8_t)((px >> 24) & 0xFF);
            uint8_t r = (uint8_t)((px >> 16) & 0xFF);
            uint8_t g = (uint8_t)((px >> 8) & 0xFF);
            uint8_t b = (uint8_t)(px & 0xFF);

            uint64_t idx = (uint64_t)dy * dst_w + dx;
            accum_r[idx] += r;
            accum_g[idx] += g;
            accum_b[idx] += b;
            accum_a[idx] += a;
            counts[idx]++;
        }
    }

    for (uint64_t i = 0; i < dst_pixels; i++) {
        uint32_t n = counts[i] ? counts[i] : 1; /* guard: a dest pixel with
                                                  * zero contributing source
                                                  * pixels (possible only if
                                                  * dst is larger than src in
                                                  * some dimension, which
                                                  * cursor_get_size's usage
                                                  * never does, but defend
                                                  * anyway) stays black/
                                                  * transparent rather than
                                                  * dividing by zero. */
        uint8_t r = (uint8_t)(accum_r[i] / n);
        uint8_t g = (uint8_t)(accum_g[i] / n);
        uint8_t b = (uint8_t)(accum_b[i] / n);
        uint8_t a = (uint8_t)(accum_a[i] / n);
        dst[i] = ((color_t)a << 24) | ((color_t)r << 16) | ((color_t)g << 8) | (color_t)b;
    }

    kfree(accum_r); kfree(accum_g); kfree(accum_b); kfree(accum_a); kfree(counts);
    return dst;
}

static int load_one_cursor(const char *path, cursor_surface_t *surf) {
    size_t file_len = 0;
    uint8_t *file_data = read_entire_file(path, &file_len);
    if (!file_data) return -1;

    png_image_t img;
    png_result_t r = png_decode(file_data, file_len, &img);
    kfree(file_data);

    if (r != PNG_OK) return -1;

    color_t *full_argb = convert_to_argb(&img);

    uint32_t src_w = img.width;
    uint32_t src_h = img.height;
    png_free(&img);

    if (!full_argb) return -1;

    /* Downscale to a sane on-screen cursor size, preserving aspect
     * ratio, unless the source is already small enough that scaling
     * would only ever upscale (which this path never does - a
     * source already <= CURSOR_TARGET_MAX_DIM in both dimensions is
     * used as-is, full resolution, no quality loss from an
     * unnecessary resample pass). */
    if (src_w <= CURSOR_TARGET_MAX_DIM && src_h <= CURSOR_TARGET_MAX_DIM) {
        surf->width = src_w;
        surf->height = src_h;
        surf->argb = full_argb;
        return 0;
    }

    uint32_t longer_dim = (src_w > src_h) ? src_w : src_h;
    uint32_t dst_w = (uint32_t)((uint64_t)src_w * CURSOR_TARGET_MAX_DIM / longer_dim);
    uint32_t dst_h = (uint32_t)((uint64_t)src_h * CURSOR_TARGET_MAX_DIM / longer_dim);
    if (dst_w < 1) dst_w = 1;
    if (dst_h < 1) dst_h = 1;

    color_t *small_argb = box_downscale(full_argb, src_w, src_h, dst_w, dst_h);
    kfree(full_argb);

    if (!small_argb) return -1;

    surf->width = dst_w;
    surf->height = dst_h;
    surf->argb = small_argb;
    return 0;
}

int cursor_assets_init(void) {
    int arrow_ok = (load_one_cursor(CURSOR_ASSET_PATH_ARROW, &g_cursor_surfaces[CURSOR_ARROW]) == 0);
    int hand_ok  = (load_one_cursor(CURSOR_ASSET_PATH_HAND,  &g_cursor_surfaces[CURSOR_HAND])  == 0);

    g_assets_loaded = (arrow_ok && hand_ok);
    return g_assets_loaded ? 0 : -1;
}

int cursor_assets_loaded(void) {
    return g_assets_loaded;
}

void cursor_assets_free(void) {
    for (int i = 0; i < 2; i++) {
        if (g_cursor_surfaces[i].argb) {
            kfree(g_cursor_surfaces[i].argb);
            g_cursor_surfaces[i].argb = 0;
        }
    }
    g_assets_loaded = 0;
}

/* Procedural fallback: a minimal solid-triangle pointer shape, used
 * only when the real PNG assets aren't available (e.g. booted from
 * the plain GRUB ISO with no TRPFS disk installed yet - see
 * kernel_init()'s installer_try_automount() path). This is
 * deliberately tiny and simple - it exists so a cursor is never
 * completely invisible, not to look polished; the real, designed
 * cursor is always the PNG asset. */
static void draw_fallback_arrow(int32_t x, int32_t y, color_t color) {
    /* A small filled triangle, ~12x16px, hand-specified as explicit
     * (dx,dy) spans per row rather than any bitmap data. */
    static const int8_t row_widths[16] = {
        1,2,3,4,5,6,7,8,9,10,6,6,3,3,0,0
    };
    for (int32_t row = 0; row < 16; row++) {
        int32_t py = y + row;
        if (py < 0) continue;
        int w = row_widths[row];
        for (int32_t col = 0; col < w; col++) {
            int32_t px = x + col;
            if (px < 0) continue;
            graphics_draw_pixel((uint32_t)px, (uint32_t)py, color);
        }
    }
}

void cursor_get_size(cursor_shape_t shape, uint32_t *out_width, uint32_t *out_height) {
    if (shape != CURSOR_ARROW && shape != CURSOR_HAND) {
        *out_width = 0;
        *out_height = 0;
        return;
    }

    if (g_assets_loaded && g_cursor_surfaces[shape].argb) {
        *out_width = g_cursor_surfaces[shape].width;
        *out_height = g_cursor_surfaces[shape].height;
    } else {
        /* Matches draw_fallback_arrow()'s actual footprint - the
         * widest row in row_widths[] is 10px, and it's 16 rows tall. */
        *out_width = 10;
        *out_height = 16;
    }
}

void cursor_draw(int32_t x, int32_t y, cursor_shape_t shape, color_t color) {
    if (shape != CURSOR_ARROW && shape != CURSOR_HAND) return;

    if (!g_assets_loaded || !g_cursor_surfaces[shape].argb) {
        draw_fallback_arrow(x, y, color);
        return;
    }

    const cursor_surface_t *surf = &g_cursor_surfaces[shape];

    for (uint32_t row = 0; row < surf->height; row++) {
        int32_t py = y + (int32_t)row;
        if (py < 0) continue;

        for (uint32_t col = 0; col < surf->width; col++) {
            int32_t px = x + (int32_t)col;
            if (px < 0) continue;

            color_t pixel = surf->argb[(uint64_t)row * surf->width + col];
            uint8_t alpha = (uint8_t)((pixel >> 24) & 0xFF);
            if (alpha == 0) continue; /* fully transparent - skip entirely */

            /* Real alpha blending (not a 1-bit on/off test like the
             * old embedded-bitmap version) - the PNG's actual alpha
             * channel, whatever antialiasing or partial transparency
             * it has, composites correctly via graphics_core's blend
             * path. */
            graphics_blend_pixel((uint32_t)px, (uint32_t)py, pixel);
        }
    }
}
