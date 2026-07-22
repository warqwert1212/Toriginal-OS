/* startmenu.c - see startmenu.h. */
#include "startmenu.h"
#include "gui_asset.h"
#include "heap.h"
#include "string.h"
#include "font8x16.h"
#include "port.h"
#include "fs.h"

/* ── Asset slots ─────────────────────────────────────────────────────── */
typedef struct {
    png_image_t raw;
    int         loaded;
    color_t    *scaled;
    uint32_t    scaled_w, scaled_h;
} asset_slot_t;

static asset_slot_t g_orb, g_orb_hover, g_taskbar, g_menu_bg, g_programs_block, g_shortcut, g_power;

static void load_slot(asset_slot_t *slot, const char *path) {
    memset(slot, 0, sizeof(*slot));
    slot->loaded = gui_asset_load_png(path, &slot->raw);
}

static void ensure_scaled(asset_slot_t *slot, uint32_t w, uint32_t h) {
    if (!slot->loaded || w == 0 || h == 0) return;
    if (slot->scaled && slot->scaled_w == w && slot->scaled_h == h) return;

    color_t *buf = gui_asset_scale_argb(&slot->raw, w, h);
    if (!buf) return;

    if (slot->scaled) kfree(slot->scaled);
    slot->scaled = buf;
    slot->scaled_w = w;
    slot->scaled_h = h;
}

/* ── Pinned shortcut list (RIGHT column) ────────────────────────────── */
typedef struct {
    char                 label[48];
    startmenu_action_fn  action;
    void                *ctx;
} program_entry_t;

static program_entry_t g_pinned[STARTMENU_MAX_PROGRAMS];
static int             g_pinned_count = 0;

int startmenu_add_program(const char *label, startmenu_action_fn action, void *ctx) {
    if (!label || g_pinned_count >= STARTMENU_MAX_PROGRAMS) return 0;
    program_entry_t *e = &g_pinned[g_pinned_count];
    strncpy(e->label, label, sizeof(e->label) - 1);
    e->label[sizeof(e->label) - 1] = '\0';
    e->action = action;
    e->ctx = ctx;
    g_pinned_count++;
    return 1;
}

/* ── Real, live-scanned program list (LEFT column) ──────────────────── */
typedef struct {
    char names[STARTMENU_MAX_LISTED][48]; /* display name - .trp suffix stripped */
    int  count;
} listed_programs_t;

static int has_trp_suffix(const char *name, uint8_t name_len) {
    return name_len > 4 &&
           name[name_len - 4] == '.' &&
           (name[name_len - 3] == 't' || name[name_len - 3] == 'T') &&
           (name[name_len - 2] == 'r' || name[name_len - 2] == 'R') &&
           (name[name_len - 1] == 'p' || name[name_len - 1] == 'P');
}

static int collect_programs_cb(const char *name, uint8_t name_len, uint8_t type, void *ctx) {
    listed_programs_t *l = (listed_programs_t *)ctx;
    if (type == FILE_TYPE_DIR) return 0;
    if (!has_trp_suffix(name, name_len)) return 0;
    if (l->count >= STARTMENU_MAX_LISTED) return 1;

    size_t n = name_len - 4; /* drop ".trp" for display */
    if (n > sizeof(l->names[0]) - 1) n = sizeof(l->names[0]) - 1;
    memcpy(l->names[l->count], name, n);
    l->names[l->count][n] = '\0';
    l->count++;
    return 0;
}

/* Rescanned every time the panel is drawn while open (see
 * startmenu_draw_panel) - this is genuinely live, not cached at
 * startmenu_init() time, so dropping a .trp into PROGRAMS_DIR shows up
 * the very next time someone opens the menu with no reboot needed. */
static listed_programs_t g_listed;

static void (*g_launch_fn)(const char *path) = NULL;
void startmenu_set_launcher(void (*launch_fn)(const char *path)) {
    g_launch_fn = launch_fn;
}

/* ── Open/close state ───────────────────────────────────────────────── */
static int g_open = 0;
void startmenu_toggle(void) { g_open = !g_open; }
void startmenu_close(void)  { g_open = 0; }
int  startmenu_is_open(void) { return g_open; }

/* ── Init ────────────────────────────────────────────────────────────── */
void startmenu_init(void) {
    load_slot(&g_orb,            STARTMENU_ORB_PATH);
    load_slot(&g_orb_hover,       STARTMENU_ORB_HOVER_PATH);
    load_slot(&g_taskbar,         STARTMENU_TASKBAR_PATH);
    load_slot(&g_menu_bg,         STARTMENU_MENU_BG_PATH);
    load_slot(&g_programs_block,  STARTMENU_PROGRAMS_BLOCK_PATH);
    load_slot(&g_shortcut,        STARTMENU_SHORTCUT_PATH);
    load_slot(&g_power,           STARTMENU_POWER_PATH);
}

/* Reads the real installed username straight off TRPFS - same file,
 * same key, same cache-nothing approach shell.c's load_username()
 * uses. Not exposed cross-module anywhere, so this reads it directly
 * rather than inventing a shared accessor for one label. */
static void get_username(char *out, size_t out_size) {
    strncpy(out, "user", out_size - 1);
    out[out_size - 1] = '\0';

    fd_t fd = fs_open("/toriginal_os/config.ini", O_RDONLY, 0);
    if (fd < 0) return;
    char buf[256];
    ssize_t n = fs_read(fd, buf, sizeof(buf) - 1);
    fs_close(fd);
    if (n <= 0) return;
    buf[n] = '\0';

    const char *key = "username=";
    size_t klen = strlen(key);
    for (char *p = buf; *p; p++) {
        if (strncmp(p, key, klen) == 0) {
            p += klen;
            size_t i = 0;
            while (*p && *p != '\n' && i < out_size - 1) out[i++] = *p++;
            out[i] = '\0';
            return;
        }
    }
}

/* ── Layout ──────────────────────────────────────────────────────────
 * Panel is sized off menu_bg.png's own aspect ratio (falls back to a
 * reasonable default if it isn't installed), scaled down to fit above
 * the taskbar exactly like before. Two columns below a header:
 *   LEFT  (58% width)  - real program list, then "all programs" bar,
 *                        then a search bar
 *   RIGHT (42% width)  - pinned shortcuts, one shortcut.png row each
 *   Power button spans the RIGHT column's width along the bottom,
 *   matching the reference layout. */
#define PANEL_NOMINAL_W 340
#define PANEL_FALLBACK_NATIVE_W 137u
#define PANEL_FALLBACK_NATIVE_H 348u

typedef struct {
    int32_t  panel_x, panel_y;
    uint32_t panel_w, panel_h;
    uint32_t header_h;

    int32_t  white_x, white_y;
    uint32_t white_w, white_h;
    uint32_t recent_h, all_programs_h, search_h;

    int32_t  shortcuts_x, shortcuts_y;
    uint32_t shortcuts_w, shortcuts_h;

    int32_t  power_x, power_y;
    uint32_t power_w, power_h;

    uint32_t row_h;

    int32_t  orb_x, orb_y;
    uint32_t orb_size;
} startmenu_geom_t;

static startmenu_geom_t g_geom;
static uint32_t g_geom_screen_h = 0, g_geom_taskbar_h = 0;

static void compute_geom(uint32_t screen_h, uint32_t taskbar_h) {
    if (g_geom_screen_h == screen_h && g_geom_taskbar_h == taskbar_h && g_geom_screen_h != 0) return;
    g_geom_screen_h = screen_h;
    g_geom_taskbar_h = taskbar_h;

    uint32_t native_w = g_menu_bg.loaded ? g_menu_bg.raw.width  : PANEL_FALLBACK_NATIVE_W;
    uint32_t native_h = g_menu_bg.loaded ? g_menu_bg.raw.height : PANEL_FALLBACK_NATIVE_H;

    uint32_t panel_w = PANEL_NOMINAL_W;
    uint32_t panel_h = (uint32_t)(((uint64_t)panel_w * native_h) / native_w);

    uint32_t avail_h = (screen_h > taskbar_h + 20) ? (screen_h - taskbar_h - 20) : taskbar_h;
    if (panel_h > avail_h) {
        panel_h = avail_h;
        panel_w = (uint32_t)(((uint64_t)panel_h * native_w) / native_h);
    }
    if (panel_w < 160) panel_w = 160;
    if (panel_h < 200) panel_h = 200;

    g_geom.panel_x = 6;
    g_geom.panel_y = (int32_t)screen_h - (int32_t)taskbar_h - (int32_t)panel_h;
    g_geom.panel_w = panel_w;
    g_geom.panel_h = panel_h;

    g_geom.header_h = (panel_h * 9) / 100;

    g_geom.power_h = (panel_h * 7) / 100;
    if (g_geom.power_h < 20) g_geom.power_h = 20;

    uint32_t content_gap = (panel_h * 2) / 100;
    uint32_t content_h = panel_h - g_geom.header_h - g_geom.power_h - content_gap;
    int32_t  content_y = g_geom.panel_y + (int32_t)g_geom.header_h;

    g_geom.white_w = (panel_w * 58) / 100;
    g_geom.shortcuts_w = panel_w - g_geom.white_w;

    g_geom.white_x = g_geom.panel_x;
    g_geom.white_y = content_y;
    g_geom.white_h = content_h;

    g_geom.shortcuts_x = g_geom.panel_x + (int32_t)g_geom.white_w;
    g_geom.shortcuts_y = content_y;
    g_geom.shortcuts_h = content_h;

    g_geom.all_programs_h = (content_h * 12) / 100;
    g_geom.search_h       = (content_h * 12) / 100;
    g_geom.recent_h       = content_h - g_geom.all_programs_h - g_geom.search_h;

    g_geom.power_x = g_geom.shortcuts_x;
    g_geom.power_y = g_geom.panel_y + (int32_t)panel_h - (int32_t)g_geom.power_h;
    g_geom.power_w = g_geom.shortcuts_w;

    g_geom.row_h = FONT8X16_HEIGHT + 8;

    g_geom.orb_size = (taskbar_h > 4) ? taskbar_h - 4 : taskbar_h;
    g_geom.orb_x = 4;
    g_geom.orb_y = (int32_t)screen_h - (int32_t)taskbar_h + (int32_t)((taskbar_h - g_geom.orb_size) / 2);
}

/* ── Drawing ─────────────────────────────────────────────────────────── */

void startmenu_draw_taskbar_bg(uint32_t screen_w, uint32_t screen_h, uint32_t taskbar_h, color_t fallback_color) {
    uint32_t taskbar_y = (screen_h > taskbar_h) ? screen_h - taskbar_h : 0;

    if (!g_taskbar.loaded) {
        graphics_fill_rect(0, taskbar_y, screen_w, taskbar_h, fallback_color);
        return;
    }
    ensure_scaled(&g_taskbar, screen_w, taskbar_h);
    if (g_taskbar.scaled) {
        gui_asset_draw_argb(g_taskbar.scaled, screen_w, taskbar_h, 0, (int32_t)taskbar_y);
    } else {
        graphics_fill_rect(0, taskbar_y, screen_w, taskbar_h, fallback_color);
    }
}

void startmenu_draw_orb(uint32_t screen_h, uint32_t taskbar_h, int32_t mouse_x, int32_t mouse_y) {
    compute_geom(screen_h, taskbar_h);

    int hovered = (mouse_x >= g_geom.orb_x && mouse_x < g_geom.orb_x + (int32_t)g_geom.orb_size &&
                   mouse_y >= g_geom.orb_y && mouse_y < g_geom.orb_y + (int32_t)g_geom.orb_size);

    asset_slot_t *slot = (hovered && g_orb_hover.loaded) ? &g_orb_hover : &g_orb;
    if (!slot->loaded) slot = g_orb.loaded ? &g_orb : NULL;

    if (slot) {
        ensure_scaled(slot, g_geom.orb_size, g_geom.orb_size);
        if (slot->scaled) {
            gui_asset_draw_argb(slot->scaled, g_geom.orb_size, g_geom.orb_size, g_geom.orb_x, g_geom.orb_y);
            return;
        }
    }

    graphics_draw_circle((uint32_t)(g_geom.orb_x + (int32_t)g_geom.orb_size / 2),
                          (uint32_t)(g_geom.orb_y + (int32_t)g_geom.orb_size / 2),
                          g_geom.orb_size / 2, graphics_rgb(70, 130, 180));
}

static void draw_label(int32_t x, int32_t y, const char *s, color_t fg) {
    int32_t cx = x;
    for (const char *p = s; *p; p++) {
        font_draw_glyph((uint32_t)cx, (uint32_t)y, *p, fg, 0, 1, 1);
        cx += FONT8X16_WIDTH;
    }
}

void startmenu_draw_panel(uint32_t screen_h, uint32_t taskbar_h) {
    if (!g_open) return;
    compute_geom(screen_h, taskbar_h);

    /* Whole-panel backdrop */
    if (g_menu_bg.loaded) {
        ensure_scaled(&g_menu_bg, g_geom.panel_w, g_geom.panel_h);
        if (g_menu_bg.scaled) {
            gui_asset_draw_argb(g_menu_bg.scaled, g_geom.panel_w, g_geom.panel_h, g_geom.panel_x, g_geom.panel_y);
        }
    } else {
        graphics_fill_rect((uint32_t)g_geom.panel_x, (uint32_t)g_geom.panel_y,
                            g_geom.panel_w, g_geom.panel_h, graphics_rgb(20, 20, 24));
    }

    /* Header text - real installed username, not a placeholder */
    char username[32];
    get_username(username, sizeof(username));
    char header[48];
    size_t n = 0;
    const char *prefix = "Welcome \"";
    for (const char *p = prefix; *p && n < sizeof(header) - 1; p++) header[n++] = *p;
    for (const char *p = username; *p && n < sizeof(header) - 1; p++) header[n++] = *p;
    if (n < sizeof(header) - 2) { header[n++] = '"'; header[n++] = '!'; }
    header[n] = '\0';
    draw_label(g_geom.panel_x + 10, g_geom.panel_y + (int32_t)(g_geom.header_h / 3),
               header, GRAPHICS_COLOR_WHITE);

    /* LEFT column: real program list + all-programs bar + search bar,
     * all backed by one white block image stretched across the whole
     * column, matching the reference layout's continuous white panel. */
    if (g_programs_block.loaded) {
        ensure_scaled(&g_programs_block, g_geom.white_w, g_geom.white_h);
        if (g_programs_block.scaled) {
            gui_asset_draw_argb(g_programs_block.scaled, g_geom.white_w, g_geom.white_h, g_geom.white_x, g_geom.white_y);
        }
    } else {
        graphics_fill_rect((uint32_t)g_geom.white_x, (uint32_t)g_geom.white_y,
                            g_geom.white_w, g_geom.white_h, GRAPHICS_COLOR_WHITE);
    }

    /* Real, live directory scan - PROGRAMS_DIR, not a hardcoded list */
    g_listed.count = 0;
    fs_readdir(PROGRAMS_DIR, collect_programs_cb, &g_listed);

    color_t dark_text = graphics_rgb(40, 40, 46);
    int32_t row_y = g_geom.white_y + 8;
    for (int i = 0; i < g_listed.count; i++) {
        if ((uint32_t)(row_y - g_geom.white_y) + g_geom.row_h > g_geom.recent_h) break;
        draw_label(g_geom.white_x + 12, row_y, g_listed.names[i], dark_text);
        row_y += (int32_t)g_geom.row_h;
    }
    if (g_listed.count == 0) {
        /* Honest empty state - no fake/demo entries */
        draw_label(g_geom.white_x + 12, g_geom.white_y + 8, "(nothing in " PROGRAMS_DIR " yet)",
                    graphics_rgb(140, 140, 140));
    }

    int32_t all_programs_y = g_geom.white_y + (int32_t)g_geom.recent_h;
    draw_label(g_geom.white_x + 12, all_programs_y + (int32_t)(g_geom.all_programs_h / 3),
               "> all programs", dark_text);

    int32_t search_y = all_programs_y + (int32_t)g_geom.all_programs_h;
    draw_label(g_geom.white_x + 12, search_y + (int32_t)(g_geom.search_h / 3),
               "Search...", graphics_rgb(120, 120, 120));

    /* RIGHT column: pinned shortcuts, one shortcut.png row each */
    int row_count = g_pinned_count > 0 ? g_pinned_count : 1;
    uint32_t shortcut_row_h = g_geom.shortcuts_h / (uint32_t)row_count;
    for (int i = 0; i < g_pinned_count; i++) {
        int32_t ry = g_geom.shortcuts_y + (int32_t)(shortcut_row_h * (uint32_t)i);
        if (g_shortcut.loaded) {
            ensure_scaled(&g_shortcut, g_geom.shortcuts_w, shortcut_row_h);
            if (g_shortcut.scaled) {
                gui_asset_draw_argb(g_shortcut.scaled, g_geom.shortcuts_w, shortcut_row_h,
                                     g_geom.shortcuts_x, ry);
            }
        } else {
            graphics_fill_rect((uint32_t)g_geom.shortcuts_x, (uint32_t)ry,
                                g_geom.shortcuts_w, shortcut_row_h, graphics_rgb(60, 90, 130));
        }
        draw_label(g_geom.shortcuts_x + 10, ry + (int32_t)(shortcut_row_h / 2 - FONT8X16_HEIGHT / 2),
                   g_pinned[i].label, GRAPHICS_COLOR_WHITE);
    }

    /* Power button along the bottom of the RIGHT column */
    if (g_power.loaded) {
        ensure_scaled(&g_power, g_geom.power_w, g_geom.power_h);
        if (g_power.scaled) {
            gui_asset_draw_argb(g_power.scaled, g_geom.power_w, g_geom.power_h, g_geom.power_x, g_geom.power_y);
        }
    } else {
        graphics_fill_rect((uint32_t)g_geom.power_x, (uint32_t)g_geom.power_y,
                            g_geom.power_w, g_geom.power_h, graphics_rgb(160, 40, 40));
    }
}

int startmenu_orb_hit(uint32_t screen_h, uint32_t taskbar_h, int32_t x, int32_t y) {
    compute_geom(screen_h, taskbar_h);
    return x >= g_geom.orb_x && x < g_geom.orb_x + (int32_t)g_geom.orb_size &&
           y >= g_geom.orb_y && y < g_geom.orb_y + (int32_t)g_geom.orb_size;
}

/* Best-effort power off - see prior header comment history: tries the
 * QEMU/Bochs debug-exit ports, falls back to a clean halt with the
 * screen cleared if that write does nothing (real hardware/VirtualBox).
 * Not real ACPI S5 - this kernel's acpi.c doesn't parse the DSDT's
 * \_S5 object yet. */
static void system_shutdown(void) {
    outw(0x604, 0x2000);
    outw(0xB004, 0x2000);

    graphics_clear_screen(GRAPHICS_COLOR_BLACK);
    __asm__ volatile ("cli");
    for (;;) __asm__ volatile ("hlt");
}

int startmenu_handle_click(uint32_t screen_h, uint32_t taskbar_h, int32_t x, int32_t y) {
    compute_geom(screen_h, taskbar_h);

    int in_panel = (x >= g_geom.panel_x && x < g_geom.panel_x + (int32_t)g_geom.panel_w &&
                    y >= g_geom.panel_y && y < g_geom.panel_y + (int32_t)g_geom.panel_h);
    if (!in_panel) {
        startmenu_close();
        return 0;
    }

    /* Power button */
    if (x >= g_geom.power_x && x < g_geom.power_x + (int32_t)g_geom.power_w &&
        y >= g_geom.power_y && y < g_geom.power_y + (int32_t)g_geom.power_h) {
        system_shutdown();
        return 1;
    }

    /* RIGHT column: pinned shortcuts */
    if (x >= g_geom.shortcuts_x && x < g_geom.shortcuts_x + (int32_t)g_geom.shortcuts_w &&
        y >= g_geom.shortcuts_y && y < g_geom.shortcuts_y + (int32_t)g_geom.shortcuts_h) {
        int row_count = g_pinned_count > 0 ? g_pinned_count : 1;
        uint32_t shortcut_row_h = g_geom.shortcuts_h / (uint32_t)row_count;
        int idx = (int)((uint32_t)(y - g_geom.shortcuts_y) / shortcut_row_h);
        if (idx >= 0 && idx < g_pinned_count) {
            if (g_pinned[idx].action) g_pinned[idx].action(g_pinned[idx].ctx);
            startmenu_close();
        }
        return 1;
    }

    /* LEFT column: real program list rows */
    if (x >= g_geom.white_x && x < g_geom.white_x + (int32_t)g_geom.white_w &&
        y >= g_geom.white_y && y < g_geom.white_y + (int32_t)g_geom.recent_h) {
        int idx = (int)((uint32_t)(y - g_geom.white_y) / g_geom.row_h);
        if (idx >= 0 && idx < g_listed.count && g_launch_fn) {
            char path[256];
            size_t dl = strlen(PROGRAMS_DIR);
            memcpy(path, PROGRAMS_DIR, dl);
            path[dl] = '/';
            size_t nl = strlen(g_listed.names[idx]);
            memcpy(path + dl + 1, g_listed.names[idx], nl);
            memcpy(path + dl + 1 + nl, ".trp", 5);
            g_launch_fn(path);
            startmenu_close();
        }
        return 1;
    }

    return 1; /* inside the panel but not on a specific control - consumed, stays open */
}
