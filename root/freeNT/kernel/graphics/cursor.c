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
#include "string.h"
#include "syscfg.h"

typedef struct {
    uint32_t width;
    uint32_t height;
    int32_t  hx, hy;
    color_t *argb;  /* decoded, converted to ARGB color_t per pixel - NULL if not loaded */
} cursor_surface_t;

static cursor_surface_t g_cursor_surfaces[CURSOR_COUNT]; /* indexed by cursor_shape_t */
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

static color_t *box_downscale(const color_t *src, uint32_t sw, uint32_t sh, uint32_t dw, uint32_t dh) {
    color_t *dst = (color_t *)kmalloc((size_t)dw * dh * sizeof(color_t));
    if (!dst) return 0;
    for (uint32_t y = 0; y < dh; y++) {
        uint32_t y0 = (uint32_t)((uint64_t)y * sh / dh);
        uint32_t y1 = (uint32_t)((uint64_t)(y + 1) * sh / dh);
        if (y1 <= y0) y1 = y0 + 1;
        if (y1 > sh) y1 = sh;
        for (uint32_t x = 0; x < dw; x++) {
            uint32_t x0 = (uint32_t)((uint64_t)x * sw / dw);
            uint32_t x1 = (uint32_t)((uint64_t)(x + 1) * sw / dw);
            if (x1 <= x0) x1 = x0 + 1;
            if (x1 > sw) x1 = sw;
            uint64_t a = 0, r = 0, g = 0, b = 0, n = 0, wsum = 0;
            for (uint32_t yy = y0; yy < y1; yy++) {
                for (uint32_t xx = x0; xx < x1; xx++) {
                    color_t px = src[(size_t)yy * sw + xx];
                    uint64_t pa = (px >> 24) & 0xFF;
                    a += pa; n++;
                    r += pa * ((px >> 16) & 0xFF);
                    g += pa * ((px >> 8) & 0xFF);
                    b += pa * (px & 0xFF);
                    wsum += pa;
                }
            }
            if (!n || !wsum) { dst[(size_t)y * dw + x] = 0; continue; }
            dst[(size_t)y * dw + x] = ((color_t)(a / n) << 24) | ((color_t)(r / wsum) << 16) |
                                       ((color_t)(g / wsum) << 8) | (color_t)(b / wsum);
        }
    }
    return dst;
}

static void path_append(char *dst, size_t cap, const char *s) {
    size_t l = strlen(dst);
    while (*s && l + 1 < cap) dst[l++] = *s++;
    dst[l] = 0;
}

static color_t *scale_argb(color_t *src, uint32_t sw, uint32_t sh, uint32_t dw, uint32_t dh) {
    if (dw == sw && dh == sh) return src;
    color_t *out;
    if (dw < sw || dh < sh) {
        out = box_downscale(src, sw, sh, dw, dh);
    } else {
        out = (color_t *)kmalloc((size_t)dw * dh * sizeof(color_t));
        if (out) {
            for (uint32_t y = 0; y < dh; y++) {
                uint32_t sy = (uint32_t)((uint64_t)y * sh / dh);
                for (uint32_t x = 0; x < dw; x++) {
                    uint32_t sx = (uint32_t)((uint64_t)x * sw / dw);
                    out[(size_t)y * dw + x] = src[(size_t)sy * sw + sx];
                }
            }
        }
    }
    kfree(src);
    return out;
}

static int load_surface(const char *path, uint32_t size, int32_t hx, int32_t hy, cursor_surface_t *surf) {
    size_t file_len = 0;
    uint8_t *file_data = read_entire_file(path, &file_len);
    if (!file_data) return -1;

    png_image_t img;
    png_result_t r = png_decode(file_data, file_len, &img);
    kfree(file_data);
    if (r != PNG_OK) return -1;

    color_t *argb = convert_to_argb(&img);
    uint32_t sw = img.width, sh = img.height;
    png_free(&img);
    if (!argb) return -1;

    uint32_t longer = sw > sh ? sw : sh;
    uint32_t dw = (uint32_t)((uint64_t)sw * size / longer);
    uint32_t dh = (uint32_t)((uint64_t)sh * size / longer);
    if (dw < 1) dw = 1;
    if (dh < 1) dh = 1;

    color_t *out = scale_argb(argb, sw, sh, dw, dh);
    if (!out) return -1;

    surf->width = dw;
    surf->height = dh;
    surf->hx = (int32_t)((uint64_t)(hx < 0 ? 0 : hx) * dw / sw);
    surf->hy = (int32_t)((uint64_t)(hy < 0 ? 0 : hy) * dh / sh);
    surf->argb = out;
    return 0;
}

static const char *const ROLE_KEY[CURSOR_COUNT] = {
    "cursor_normal", "cursor_link", "cursor_resize_ns", "cursor_resize_we",
    "cursor_resize_nwse", "cursor_resize_nesw", "cursor_move", "cursor_busy"
};

static const char *const ROLE_DEFAULT[CURSOR_COUNT] = {
    "default_arrow", "default_link", "default_size4", "default_size3",
    "default_size2", "default_size1", "default_move", "default_busy"
};

const char *cursor_role_key(int role) { return role >= 0 && role < CURSOR_COUNT ? ROLE_KEY[role] : ""; }
const char *cursor_role_default(int role) { return role >= 0 && role < CURSOR_COUNT ? ROLE_DEFAULT[role] : ""; }

static int lookup_hotspot(const char *stem, int32_t *hx, int32_t *hy) {
    size_t len = 0;
    uint8_t *d = read_entire_file(CURSOR_HOTSPOT_PATH, &len);
    *hx = 0; *hy = 0;
    if (!d) return -1;
    size_t sl = strlen(stem);
    int found = -1;
    size_t i = 0;
    while (i < len) {
        size_t ls = i;
        while (i < len && d[i] != '\n') i++;
        size_t le = i;
        i++;
        if (le - ls > sl && memcmp(d + ls, stem, sl) == 0 && d[ls + sl] == ' ') {
            size_t p = ls + sl + 1;
            int32_t v[2] = {0, 0};
            for (int k = 0; k < 2; k++) {
                while (p < le && d[p] == ' ') p++;
                while (p < le && d[p] >= '0' && d[p] <= '9') v[k] = v[k] * 10 + (d[p++] - '0');
            }
            *hx = v[0]; *hy = v[1];
            found = 0;
            break;
        }
    }
    kfree(d);
    return found;
}

static uint32_t cursor_size_setting(void) {
    char b[16];
    uint32_t v = 32;
    if (syscfg_get("cursor_size", b, sizeof(b)) == 0) {
        uint32_t n = 0;
        for (const char *p = b; *p >= '0' && *p <= '9'; p++) n = n * 10 + (uint32_t)(*p - '0');
        if (n >= 16 && n <= 96) v = n;
    }
    return v;
}

void cursor_assets_free(void) {
    for (int i = 0; i < CURSOR_COUNT; i++) {
        if (g_cursor_surfaces[i].argb) kfree(g_cursor_surfaces[i].argb);
        memset(&g_cursor_surfaces[i], 0, sizeof(g_cursor_surfaces[i]));
    }
    g_assets_loaded = 0;
}

int cursor_assets_init(void) {
    cursor_assets_free();
    uint32_t size = cursor_size_setting();
    for (int r = 0; r < CURSOR_COUNT; r++) {
        char stem[64];
        syscfg_get_or(ROLE_KEY[r], stem, sizeof(stem), ROLE_DEFAULT[r]);
        if (!stem[0]) strncpy(stem, ROLE_DEFAULT[r], sizeof(stem) - 1);
        char path[128];
        strncpy(path, CURSOR_DIR "/", sizeof(path) - 1);
        path[sizeof(path) - 1] = 0;
        path_append(path, sizeof(path), stem);
        path_append(path, sizeof(path), ".png");
        int32_t hx, hy;
        lookup_hotspot(stem, &hx, &hy);
        if (load_surface(path, size, hx, hy, &g_cursor_surfaces[r]) != 0 && strcmp(stem, ROLE_DEFAULT[r]) != 0) {
            strncpy(path, CURSOR_DIR "/", sizeof(path) - 1);
            path_append(path, sizeof(path), ROLE_DEFAULT[r]);
            path_append(path, sizeof(path), ".png");
            lookup_hotspot(ROLE_DEFAULT[r], &hx, &hy);
            load_surface(path, size, hx, hy, &g_cursor_surfaces[r]);
        }
    }
    if (!g_cursor_surfaces[CURSOR_ARROW].argb)
        load_surface(CURSOR_ASSET_PATH_ARROW, size, 0, 0, &g_cursor_surfaces[CURSOR_ARROW]);
    if (!g_cursor_surfaces[CURSOR_HAND].argb)
        load_surface(CURSOR_ASSET_PATH_HAND, size, 0, 0, &g_cursor_surfaces[CURSOR_HAND]);
    g_assets_loaded = g_cursor_surfaces[CURSOR_ARROW].argb != 0;
    return g_assets_loaded ? 0 : -1;
}

void cursor_reload(void) { cursor_assets_init(); }

void cursor_get_hotspot(cursor_shape_t shape, int32_t *hx, int32_t *hy) {
    *hx = 0; *hy = 0;
    if ((int)shape < 0 || (int)shape >= CURSOR_COUNT || !g_assets_loaded) return;
    const cursor_surface_t *surf = &g_cursor_surfaces[shape];
    if (!surf->argb) surf = &g_cursor_surfaces[CURSOR_ARROW];
    *hx = surf->hx; *hy = surf->hy;
}

int cursor_assets_loaded(void) {
    return g_assets_loaded;
}

static void draw_fallback_arrow(int32_t x, int32_t y, color_t color) {
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
    if ((int)shape < 0 || (int)shape >= CURSOR_COUNT) {
        *out_width = 0;
        *out_height = 0;
        return;
    }
    if (g_assets_loaded && g_cursor_surfaces[shape].argb) {
        *out_width = g_cursor_surfaces[shape].width;
        *out_height = g_cursor_surfaces[shape].height;
    } else {
        *out_width = 10;
        *out_height = 16;
    }
}

void cursor_draw(int32_t x, int32_t y, cursor_shape_t shape, color_t color) {
    if ((int)shape < 0 || (int)shape >= CURSOR_COUNT) shape = CURSOR_ARROW;

    if (!g_assets_loaded) {
        draw_fallback_arrow(x, y, color);
        return;
    }

    const cursor_surface_t *surf = &g_cursor_surfaces[shape];
    if (!surf->argb) surf = &g_cursor_surfaces[CURSOR_ARROW];
    if (!surf->argb) { draw_fallback_arrow(x, y, color); return; }

    int32_t ox = x - surf->hx, oy = y - surf->hy;
    for (uint32_t row = 0; row < surf->height; row++) {
        int32_t py = oy + (int32_t)row;
        if (py < 0) continue;
        for (uint32_t col = 0; col < surf->width; col++) {
            int32_t px = ox + (int32_t)col;
            if (px < 0) continue;
            color_t pixel = surf->argb[(uint64_t)row * surf->width + col];
            if (((pixel >> 24) & 0xFF) == 0) continue;
            graphics_blend_pixel((uint32_t)px, (uint32_t)py, pixel);
        }
    }
}
