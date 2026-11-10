#include "app_abi.h"
#include "syscall.h"
#include "process.h"
#include "paging.h"
#include "wm.h"
#include "fs.h"
#include "string.h"
#include "heap.h"
#include "syscfg.h"
#include "pit.h"
#include "pmm.h"
#include "net.h"
#include "kernel.h"
#include "graphics_core.h"
#include "font8x16.h"
#include "imgdec.h"
#include "png.h"
#include "crash.h"
#include "serial.h"

#define IMG_CACHE_MAX 40

typedef struct {
    char      path[80];
    int       w, h;
    uint32_t *px;
    uint32_t  stamp;
} img_cache_t;

static img_cache_t g_img[IMG_CACHE_MAX];
static uint32_t    g_img_clock = 0;

static int user_ok(uint64_t p, uint64_t n) {
    if (p < 0x1000) return 0;
    if (n && p + n < p) return 0;
    return paging_user_range_ok(p, n ? n : 1);
}

static int user_str(char *dst, size_t cap, uint64_t p) {
    if (!user_ok(p, 1)) return -1;
    size_t i = 0;
    for (; i + 1 < cap; i++) {
        if (!user_ok(p + i, 1)) return -1;
        char c = ((const char *)(uintptr_t)p)[i];
        dst[i] = c;
        if (!c) return 0;
    }
    dst[i] = 0;
    return 0;
}

static wm_window_t *owned_window(uint32_t id) {
    process_t *cur = process_get_current();
    if (!cur || !id) return NULL;
    for (int i = 0; i < PROCESS_MAX_OWNED_WINDOWS; i++)
        if (cur->owned_windows[i] == id) return wm_get_window(id);
    return NULL;
}

static void fill_rect(wm_window_t *w, int x, int y, int rw, int rh, uint32_t color) {
    int x0 = x < 0 ? 0 : x, y0 = y < 0 ? 0 : y;
    int x1 = x + rw, y1 = y + rh;
    if (x1 > w->client_w) x1 = w->client_w;
    if (y1 > w->client_h) y1 = w->client_h;
    if (x0 >= x1 || y0 >= y1) return;
    color |= 0xFF000000u;
    for (int yy = y0; yy < y1; yy++) {
        uint32_t *row = (uint32_t *)((uint8_t *)w->pixels + (size_t)yy * (size_t)w->pitch);
        for (int xx = x0; xx < x1; xx++) row[xx] = color;
    }
}

static const uint8_t *glyph_rows(uint8_t code) {
    if (font_get_style() == FONT_STYLE_MONO && code >= 32 && code <= 126)
        return font_mono8x16_data[code - 32];
    return font8x16_data[code];
}

static void draw_text(wm_window_t *w, int x, int y, const char *s, uint32_t fg, uint32_t bg, uint32_t flags) {
    int scale = (int)((flags >> GUI_F_SCALE_SHIFT) & 0xFF);
    if (scale < 1) scale = 1;
    if (scale > 4) scale = 4;
    int transparent = (flags & GUI_F_TRANSPARENT) != 0;
    fg |= 0xFF000000u;
    bg |= 0xFF000000u;
    for (; *s; s++, x += 8 * scale) {
        if (x >= w->client_w) break;
        if (x + 8 * scale <= 0) continue;
        const uint8_t *rows = glyph_rows((uint8_t)*s);
        for (int r = 0; r < 16; r++) {
            uint8_t bits = rows[r];
            for (int c = 0; c < 8; c++) {
                int set = (bits >> (7 - c)) & 1;
                if (!set && transparent) continue;
                uint32_t col = set ? fg : bg;
                for (int sy = 0; sy < scale; sy++) {
                    int py = y + r * scale + sy;
                    if (py < 0 || py >= w->client_h) continue;
                    uint32_t *row = (uint32_t *)((uint8_t *)w->pixels + (size_t)py * (size_t)w->pitch);
                    for (int sx = 0; sx < scale; sx++) {
                        int px = x + c * scale + sx;
                        if (px >= 0 && px < w->client_w) row[px] = col;
                    }
                }
            }
        }
    }
}

static uint32_t *scale_image(const png_image_t *im, int tw, int th) {
    uint32_t *out = (uint32_t *)kmalloc((size_t)tw * (size_t)th * 4);
    if (!out) return NULL;
    int ch = im->channels;
    for (int y = 0; y < th; y++) {
        uint32_t sy0 = (uint32_t)((uint64_t)y * im->height / (uint32_t)th);
        uint32_t sy1 = (uint32_t)((uint64_t)(y + 1) * im->height / (uint32_t)th);
        if (sy1 <= sy0) sy1 = sy0 + 1;
        for (int x = 0; x < tw; x++) {
            uint32_t sx0 = (uint32_t)((uint64_t)x * im->width / (uint32_t)tw);
            uint32_t sx1 = (uint32_t)((uint64_t)(x + 1) * im->width / (uint32_t)tw);
            if (sx1 <= sx0) sx1 = sx0 + 1;
            uint32_t r = 0, g = 0, b = 0, a = 0, n = 0;
            for (uint32_t yy = sy0; yy < sy1 && yy < im->height; yy++) {
                for (uint32_t xx = sx0; xx < sx1 && xx < im->width; xx++) {
                    const uint8_t *p = im->pixels + ((size_t)yy * im->width + xx) * (size_t)ch;
                    uint32_t pa = ch == 4 ? p[3] : 255;
                    r += p[0] * pa; g += p[1] * pa; b += p[2] * pa; a += pa; n++;
                }
            }
            uint32_t oa = n ? a / n : 0;
            uint32_t orr = a ? r / a : 0, og = a ? g / a : 0, ob = a ? b / a : 0;
            out[y * tw + x] = (oa << 24) | (orr << 16) | (og << 8) | ob;
        }
    }
    return out;
}

static img_cache_t *image_lookup(const char *path, int w, int h) {
    for (int i = 0; i < IMG_CACHE_MAX; i++) {
        if (g_img[i].px && g_img[i].w == w && g_img[i].h == h && strcmp(g_img[i].path, path) == 0) {
            g_img[i].stamp = ++g_img_clock;
            return &g_img[i];
        }
    }
    inode_t st;
    if (fs_stat(path, &st) != 0 || st.size == 0 || st.size > 16u * 1024u * 1024u) return NULL;
    fd_t fd = fs_open(path, O_RDONLY, 0);
    if (fd < 0) return NULL;
    uint8_t *buf = (uint8_t *)kmalloc((size_t)st.size);
    if (!buf) { fs_close(fd); return NULL; }
    ssize_t n = fs_read(fd, buf, (size_t)st.size);
    fs_close(fd);
    if (n != (ssize_t)st.size) { kfree(buf); return NULL; }
    png_image_t im;
    int rc = image_decode(buf, (size_t)st.size, &im);
    kfree(buf);
    if (rc != 0 || !im.pixels) return NULL;
    uint32_t *px = scale_image(&im, w, h);
    png_free(&im);
    if (!px) return NULL;

    int slot = -1;
    uint32_t oldest = 0xFFFFFFFFu;
    for (int i = 0; i < IMG_CACHE_MAX; i++) {
        if (!g_img[i].px) { slot = i; break; }
        if (g_img[i].stamp < oldest) { oldest = g_img[i].stamp; slot = i; }
    }
    if (g_img[slot].px) kfree(g_img[slot].px);
    strncpy(g_img[slot].path, path, sizeof(g_img[slot].path) - 1);
    g_img[slot].path[sizeof(g_img[slot].path) - 1] = 0;
    g_img[slot].w = w;
    g_img[slot].h = h;
    g_img[slot].px = px;
    g_img[slot].stamp = ++g_img_clock;
    return &g_img[slot];
}

static void blit_image(wm_window_t *win, int x, int y, img_cache_t *c) {
    for (int yy = 0; yy < c->h; yy++) {
        int py = y + yy;
        if (py < 0 || py >= win->client_h) continue;
        uint32_t *row = (uint32_t *)((uint8_t *)win->pixels + (size_t)py * (size_t)win->pitch);
        for (int xx = 0; xx < c->w; xx++) {
            int px = x + xx;
            if (px < 0 || px >= win->client_w) continue;
            uint32_t s = c->px[yy * c->w + xx];
            uint32_t a = s >> 24;
            if (a == 0) continue;
            if (a == 255) { row[px] = s | 0xFF000000u; continue; }
            uint32_t d = row[px];
            uint32_t r = (((s >> 16) & 255) * a + ((d >> 16) & 255) * (255 - a)) / 255;
            uint32_t g = (((s >> 8) & 255) * a + ((d >> 8) & 255) * (255 - a)) / 255;
            uint32_t b = ((s & 255) * a + (d & 255) * (255 - a)) / 255;
            row[px] = 0xFF000000u | (r << 16) | (g << 8) | b;
        }
    }
}

static size_t put_u(char *d, size_t n, size_t cap, uint64_t v) {
    char t[20]; int k = 0;
    if (!v) t[k++] = '0';
    while (v && k < 20) { t[k++] = (char)('0' + v % 10); v /= 10; }
    while (k && n + 1 < cap) d[n++] = t[--k];
    d[n] = 0;
    return n;
}

static size_t put_s(char *d, size_t n, size_t cap, const char *s) {
    while (*s && n + 1 < cap) d[n++] = *s++;
    d[n] = 0;
    return n;
}

static uint64_t sys_gui_fail(const char *why, uint32_t win) {
    char d[128]; size_t n = 0;
    process_t *cur = process_get_current();
    d[0] = 0;
    n = put_s(d, n, sizeof(d), why);
    n = put_s(d, n, sizeof(d), " win=");
    n = put_u(d, n, sizeof(d), win);
    n = put_s(d, n, sizeof(d), " pid=");
    n = put_u(d, n, sizeof(d), cur ? (uint64_t)cur->pid : 0);
    n = put_s(d, n, sizeof(d), " own=");
    if (cur) for (int i = 0; i < PROCESS_MAX_OWNED_WINDOWS && i < 4; i++) {
        n = put_u(d, n, sizeof(d), cur->owned_windows[i]);
        n = put_s(d, n, sizeof(d), ",");
    }
    n = put_s(d, n, sizeof(d), " wm=");
    n = put_u(d, n, sizeof(d), wm_get_window(win) ? 1 : 0);
    gui_error_report("GUI call failed", d);
    return (uint64_t)-1;
}

static uint64_t sys_gui(uint64_t op, uint64_t ptr, uint64_t a3, uint64_t a4, uint64_t a5, uint64_t a6) {
    (void)a3; (void)a4; (void)a5; (void)a6;
    gui_cmd_t cmd;
    if (!user_ok(ptr, sizeof(cmd))) return sys_gui_fail("bad ptr", 0);
    memcpy(&cmd, (const void *)(uintptr_t)ptr, sizeof(cmd));
    wm_window_t *w = owned_window(cmd.win);
    if (!w) return sys_gui_fail("not owned", cmd.win);
    if (!w->pixels) return sys_gui_fail("no pixels", cmd.win);

    if (op == GUI_FILL) {
        fill_rect(w, cmd.x, cmd.y, cmd.w, cmd.h, cmd.fg);
        return 0;
    }
    if (op == GUI_TEXT) {
        char s[257];
        if (user_str(s, sizeof(s), cmd.ptr) != 0) return (uint64_t)-1;
        draw_text(w, cmd.x, cmd.y, s, cmd.fg, cmd.bg, cmd.flags);
        return 0;
    }
    if (op == GUI_IMAGE) {
        char path[96];
        if (cmd.w < 1 || cmd.h < 1 || cmd.w > 512 || cmd.h > 512) return (uint64_t)-1;
        if (user_str(path, sizeof(path), cmd.ptr) != 0) return (uint64_t)-1;
        img_cache_t *c = image_lookup(path, cmd.w, cmd.h);
        if (!c) return (uint64_t)-1;
        blit_image(w, cmd.x, cmd.y, c);
        return 0;
    }
    if (op == GUI_TITLE) {
        char t[64];
        if (user_str(t, sizeof(t), cmd.ptr) != 0) return (uint64_t)-1;
        strncpy(w->title, t, sizeof(w->title) - 1);
        w->title[sizeof(w->title) - 1] = 0;
        return 0;
    }
    return (uint64_t)-1;
}

typedef struct {
    uint8_t *buf;
    size_t   max;
    size_t   len;
    int      count;
} dir_ctx_t;

static int dir_cb(const char *name, uint8_t name_len, uint8_t type, void *ctx) {
    dir_ctx_t *d = (dir_ctx_t *)ctx;
    if (name_len == 0) return 0;
    if (name_len == 1 && name[0] == '.') return 0;
    if (name_len == 2 && name[0] == '.' && name[1] == '.') return 0;
    if (d->len + (size_t)name_len + 2 > d->max) return 1;
    d->buf[d->len++] = (type == FILE_TYPE_DIR) ? DIRENT_DIR : DIRENT_FILE;
    memcpy(d->buf + d->len, name, name_len);
    d->len += name_len;
    d->buf[d->len++] = 0;
    d->count++;
    return 0;
}

static uint64_t sys_appsvc(uint64_t op, uint64_t a, uint64_t b, uint64_t c, uint64_t a5, uint64_t a6) {
    (void)a5; (void)a6;
    char s1[160];

    if (op == SVC_READDIR) {
        if (user_str(s1, sizeof(s1), a) != 0 || !user_ok(b, c)) return (uint64_t)-1;
        static uint8_t tmp[8192];
        dir_ctx_t d = { tmp, c < sizeof(tmp) ? (size_t)c : sizeof(tmp), 0, 0 };
        if (fs_readdir(s1, dir_cb, &d) != 0) return (uint64_t)-1;
        memcpy((void *)(uintptr_t)b, tmp, d.len);
        return (uint64_t)d.count;
    }
    if (op == SVC_CFG_GET) {
        char out[128];
        if (user_str(s1, sizeof(s1), a) != 0 || !user_ok(b, c) || c == 0) return (uint64_t)-1;
        int ok = syscfg_get(s1, out, sizeof(out));
        size_t n = strlen(out);
        if (n >= c) n = (size_t)c - 1;
        memcpy((void *)(uintptr_t)b, out, n);
        ((char *)(uintptr_t)b)[n] = 0;
        return ok ? 0 : (uint64_t)-1;
    }
    if (op == SVC_CFG_SET) {
        char val[128];
        if (user_str(s1, sizeof(s1), a) != 0 || user_str(val, sizeof(val), b) != 0) return (uint64_t)-1;
        return (uint64_t)(int64_t)syscfg_set(s1, val);
    }
    if (op == SVC_UPTIME_MS) return pit_get_milliseconds();
    if (op == SVC_STAT) {
        app_stat_t o;
        inode_t st;
        if (user_str(s1, sizeof(s1), a) != 0 || !user_ok(b, sizeof(o))) return (uint64_t)-1;
        if (fs_stat(s1, &st) != 0) return (uint64_t)-1;
        memset(&o, 0, sizeof(o));
        o.size = st.size;
        o.type = FS_IS_DIR(st.mode) ? DIRENT_DIR : DIRENT_FILE;
        o.mtime = st.modified_time;
        memcpy((void *)(uintptr_t)b, &o, sizeof(o));
        return 0;
    }
    if (op == SVC_SYSINFO) {
        app_sysinfo_t o;
        if (!user_ok(a, sizeof(o))) return (uint64_t)-1;
        memset(&o, 0, sizeof(o));
        o.ram_total_kb = pmm_get_total_ram_kb();
        o.ram_free_kb = pmm_get_free_ram();
        o.fb_width = g_framebuffer.width;
        o.fb_height = g_framebuffer.height;
        o.uptime_ms = pit_get_milliseconds();
        net_device_t *nd = net_get_device();
        if (nd) {
            o.has_nic = 1;
            o.ip = nd->ip;
            memcpy(o.mac, nd->mac, 6);
        }
        strncpy(o.os_name, "Toriginal OS", sizeof(o.os_name) - 1);
        strncpy(o.os_version, KERNEL_VERSION " build " BUILD_NUMBER, sizeof(o.os_version) - 1);
        strncpy(o.kernel_name, KERNEL_NAME, sizeof(o.kernel_name) - 1);
        strncpy(o.arch, "x86-64", sizeof(o.arch) - 1);
        memcpy((void *)(uintptr_t)a, &o, sizeof(o));
        return 0;
    }
    return (uint64_t)-1;
}

void appsvc_register(void (*reg)(uint32_t, syscall_handler_t)) {
    reg(SYS_GUI, sys_gui);
    reg(SYS_APPSVC, sys_appsvc);
}
