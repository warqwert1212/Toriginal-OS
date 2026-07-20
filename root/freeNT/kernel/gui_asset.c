/* gui_asset.c - see gui_asset.h. */
#include "gui_asset.h"
#include "fs.h"
#include "heap.h"
#include "string.h"

int gui_asset_load_png(const char *path, png_image_t *out) {
    memset(out, 0, sizeof(*out));
    if (!path || !path[0]) return 0;

    inode_t st;
    if (fs_stat(path, &st) != 0 || !FS_IS_FILE(st.mode)) return 0;

    fd_t fd = fs_open(path, O_RDONLY, 0);
    if (fd < 0) return 0;

    uint8_t *buf = (uint8_t *)kmalloc((size_t)st.size);
    if (!buf) { fs_close(fd); return 0; }

    size_t total = 0;
    while (total < (size_t)st.size) {
        ssize_t n = fs_read(fd, buf + total, (size_t)st.size - total);
        if (n <= 0) break;
        total += (size_t)n;
    }
    fs_close(fd);

    if (total != (size_t)st.size) { kfree(buf); return 0; }

    png_result_t r = png_decode(buf, total, out);
    kfree(buf);
    return r == PNG_OK && out->pixels != NULL;
}

color_t *gui_asset_scale_argb(const png_image_t *src, uint32_t dst_w, uint32_t dst_h) {
    if (!src || !src->pixels || dst_w == 0 || dst_h == 0) return NULL;
    if (src->width == 0 || src->height == 0) return NULL;

    color_t *buf = (color_t *)kmalloc((size_t)dst_w * dst_h * sizeof(color_t));
    if (!buf) return NULL;

    uint32_t sw = src->width;
    uint32_t sh = src->height;
    uint8_t  ch = src->channels;

    for (uint32_t y = 0; y < dst_h; y++) {
        uint32_t sy = (y * sh) / dst_h;
        const uint8_t *row = src->pixels + (size_t)sy * sw * ch;
        for (uint32_t x = 0; x < dst_w; x++) {
            uint32_t sx = (x * sw) / dst_w;
            const uint8_t *px = row + (size_t)sx * ch;
            uint8_t a = (ch == 4) ? px[3] : 255;
            buf[(size_t)y * dst_w + x] = graphics_argb(a, px[0], px[1], px[2]);
        }
    }
    return buf;
}

void gui_asset_draw_argb(const color_t *buf, uint32_t w, uint32_t h,
                          int32_t dst_x, int32_t dst_y) {
    if (!buf) return;
    for (uint32_t y = 0; y < h; y++) {
        int32_t sy = dst_y + (int32_t)y;
        if (sy < 0) continue;
        for (uint32_t x = 0; x < w; x++) {
            int32_t sx = dst_x + (int32_t)x;
            if (sx < 0) continue;
            graphics_blend_pixel((uint32_t)sx, (uint32_t)sy, buf[(size_t)y * w + x]);
        }
    }
}
