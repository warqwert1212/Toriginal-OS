/* wallpaper.c - see wallpaper.h for the contract.
 *
 * Everything the desktop shows here comes from a file: the PNG bytes
 * are read from TRPFS, decoded with png_decode() (already in this
 * kernel - see png.c), and the chosen path is round-tripped through
 * WALLPAPER_CFG_PATH so a reboot doesn't lose the choice. Nothing is
 * compiled into the binary. If no wallpaper file exists yet, or the
 * one on disk fails to decode, wallpaper_draw() falls back to a flat
 * color instead of pretending something loaded - that's an honest
 * "nothing installed yet" state, not a bug to hide.
 */
#include "wallpaper.h"
#include "fs.h"
#include "png.h"
#include "heap.h"
#include "graphics_2d.h"
#include "string.h"

#define WP_MAX_PATH     256
#define WP_MAX_ENTRIES  64
#define WP_MAX_NAME     64

static png_image_t g_decoded    = {0};   /* raw decoded PNG, kept so a screen resize can rescale without re-reading the file */
static color_t     *g_scaled    = NULL;  /* pre-scaled to the last-requested screen size */
static uint32_t     g_scaled_w  = 0;
static uint32_t     g_scaled_h  = 0;
static int           g_loaded    = 0;
static char          g_current_path[WP_MAX_PATH] = {0};

/* ── tiny local mkdir -p, scoped to this file (installer.c has its own
 * private copy - not worth exposing a shared one for two call sites) ── */
static void ensure_dir(const char *path) {
    char tmp[WP_MAX_PATH];
    size_t len = strlen(path);
    if (len == 0 || len >= sizeof(tmp)) return;
    memcpy(tmp, path, len + 1);

    for (size_t i = 1; i < len; i++) {
        if (tmp[i] != '/') continue;
        tmp[i] = '\0';
        inode_t st;
        if (fs_stat(tmp, &st) != 0) {
            fs_mkdir(tmp, FILE_PERM_OWNER_R | FILE_PERM_OWNER_W | FILE_PERM_OWNER_X);
        }
        tmp[i] = '/';
    }
}

static void free_scaled(void) {
    if (g_scaled) { kfree(g_scaled); g_scaled = NULL; }
    g_scaled_w = 0;
    g_scaled_h = 0;
}

static void free_decoded(void) {
    if (g_decoded.pixels) png_free(&g_decoded);
    g_decoded.pixels = NULL;
    g_decoded.width = 0;
    g_decoded.height = 0;
}

static int write_config(const char *path) {
    ensure_dir(WALLPAPER_CFG_PATH);
    fd_t fd = fs_open(WALLPAPER_CFG_PATH, O_WRONLY | O_CREAT | O_TRUNC,
                       FILE_PERM_OWNER_R | FILE_PERM_OWNER_W);
    if (fd < 0) return -1;
    size_t n = strlen(path);
    int ok = (fs_write(fd, path, n) == (ssize_t)n);
    fs_close(fd);
    return ok ? 0 : -1;
}

int wallpaper_set_path(const char *path) {
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

    png_image_t decoded = {0};
    png_result_t r = png_decode(buf, total, &decoded);
    kfree(buf);
    if (r != PNG_OK || !decoded.pixels) return 0;

    /* Decode succeeded - safe to replace the live wallpaper now. */
    free_decoded();
    g_decoded = decoded;
    free_scaled(); /* force a rescale on the next draw at whatever screen size is current */

    strncpy(g_current_path, path, sizeof(g_current_path) - 1);
    g_current_path[sizeof(g_current_path) - 1] = '\0';
    g_loaded = 1;

    write_config(path); /* best-effort - a failed persist still leaves the wallpaper active for this session */
    return 1;
}

static void rescale_to(uint32_t w, uint32_t h) {
    if (!g_decoded.pixels || w == 0 || h == 0) return;
    if (g_scaled && g_scaled_w == w && g_scaled_h == h) return;

    color_t *buf = (color_t *)kmalloc((size_t)w * h * sizeof(color_t));
    if (!buf) return; /* draw() falls back to flat color when g_scaled stays NULL */

    uint32_t sw = g_decoded.width;
    uint32_t sh = g_decoded.height;
    uint8_t ch = g_decoded.channels;

    for (uint32_t y = 0; y < h; y++) {
        uint32_t sy = (y * sh) / h;
        const uint8_t *row = g_decoded.pixels + (size_t)sy * sw * ch;
        for (uint32_t x = 0; x < w; x++) {
            uint32_t sx = (x * sw) / w;
            const uint8_t *px = row + (size_t)sx * ch;
            buf[(size_t)y * w + x] = graphics_rgb(px[0], px[1], px[2]);
        }
    }

    free_scaled();
    g_scaled = buf;
    g_scaled_w = w;
    g_scaled_h = h;
}

void wallpaper_draw(uint32_t screen_w, uint32_t screen_h, color_t fallback_color) {
    if (!g_loaded || !g_decoded.pixels) {
        graphics_clear_screen(fallback_color);
        return;
    }

    rescale_to(screen_w, screen_h);
    if (!g_scaled) {
        graphics_clear_screen(fallback_color);
        return;
    }

    gfx2d_surface_t view;
    view.width  = screen_w;
    view.height = screen_h;
    view.pixels = g_scaled;
    gfx2d_blit(&view, 0, 0);
}

int wallpaper_is_loaded(void) {
    return g_loaded;
}

/* ── directory scan / cycle ─────────────────────────────────────────── */

typedef struct {
    char names[WP_MAX_ENTRIES][WP_MAX_NAME];
    int  count;
} wp_listing_t;

static int has_png_suffix(const char *name, uint8_t name_len) {
    return name_len > 4 &&
           name[name_len - 4] == '.' &&
           (name[name_len - 3] == 'p' || name[name_len - 3] == 'P') &&
           (name[name_len - 2] == 'n' || name[name_len - 2] == 'N') &&
           (name[name_len - 1] == 'g' || name[name_len - 1] == 'G');
}

static int collect_cb(const char *name, uint8_t name_len, uint8_t type, void *ctx) {
    wp_listing_t *l = (wp_listing_t *)ctx;
    if (type == FILE_TYPE_DIR) return 0;
    if (!has_png_suffix(name, name_len)) return 0;
    if (l->count >= WP_MAX_ENTRIES) return 1; /* full - stop scanning */

    size_t n = name_len < WP_MAX_NAME - 1 ? name_len : WP_MAX_NAME - 1;
    memcpy(l->names[l->count], name, n);
    l->names[l->count][n] = '\0';
    l->count++;
    return 0;
}

void wallpaper_next(void) {
    wp_listing_t listing = {0};
    if (fs_readdir(WALLPAPER_DIR, collect_cb, &listing) != 0) return;
    if (listing.count == 0) return;

    /* Find the current file's basename in the listing so "next" means
     * next, not "always the first entry" - if nothing's active yet, or
     * the current file isn't in this directory, start at index 0. */
    const char *cur_base = g_current_path;
    for (const char *p = g_current_path; *p; p++) {
        if (*p == '/') cur_base = p + 1;
    }

    int idx = -1;
    for (int i = 0; i < listing.count; i++) {
        if (strcmp(listing.names[i], cur_base) == 0) { idx = i; break; }
    }
    int next_idx = (idx + 1) % listing.count;

    char path[WP_MAX_PATH];
    size_t dir_len = strlen(WALLPAPER_DIR);
    memcpy(path, WALLPAPER_DIR, dir_len);
    path[dir_len] = '/';
    strncpy(path + dir_len + 1, listing.names[next_idx], sizeof(path) - dir_len - 2);
    path[sizeof(path) - 1] = '\0';

    wallpaper_set_path(path);
}

void wallpaper_init(void) {
    fd_t fd = fs_open(WALLPAPER_CFG_PATH, O_RDONLY, 0);
    if (fd < 0) return; /* nothing configured yet - honest "unset" state */

    char buf[WP_MAX_PATH];
    ssize_t n = fs_read(fd, buf, sizeof(buf) - 1);
    fs_close(fd);
    if (n <= 0) return;

    buf[n] = '\0';
    for (ssize_t i = 0; i < n; i++) {
        if (buf[i] == '\n' || buf[i] == '\r') { buf[i] = '\0'; break; }
    }
    if (buf[0]) wallpaper_set_path(buf);
}
