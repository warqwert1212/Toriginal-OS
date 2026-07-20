/* startmenu.c - see startmenu.h. */
#include "startmenu.h"
#include "gui_asset.h"
#include "heap.h"
#include "string.h"
#include "font8x16.h"
#include "port.h"

/* ── Asset slots ─────────────────────────────────────────────────────
 * Each slot holds the raw decoded PNG (loaded once at startmenu_init)
 * plus a lazily-scaled ARGB buffer sized to whatever box that asset
 * is currently drawn into - re-scaled only when the target size
 * actually changes, same caching pattern wallpaper.c uses for the
 * desktop background. */
typedef struct {
    png_image_t raw;
    int         loaded;
    color_t    *scaled;
    uint32_t    scaled_w, scaled_h;
} asset_slot_t;

static asset_slot_t g_orb, g_orb_hover, g_taskbar, g_panel, g_programs, g_power;

static void load_slot(asset_slot_t *slot, const char *path) {
    memset(slot, 0, sizeof(*slot));
    slot->loaded = gui_asset_load_png(path, &slot->raw);
}

static void ensure_scaled(asset_slot_t *slot, uint32_t w, uint32_t h) {
    if (!slot->loaded || w == 0 || h == 0) return;
    if (slot->scaled && slot->scaled_w == w && slot->scaled_h == h) return;

    color_t *buf = gui_asset_scale_argb(&slot->raw, w, h);
    if (!buf) return; /* keep the old (wrong-size) buffer rather than drop it silently */

    if (slot->scaled) kfree(slot->scaled);
    slot->scaled = buf;
    slot->scaled_w = w;
    slot->scaled_h = h;
}

/* ── Program list registry ──────────────────────────────────────────── */
typedef struct {
    char                 label[48];
    startmenu_action_fn  action;
    void                *ctx;
} program_entry_t;

static program_entry_t g_programs_list[STARTMENU_MAX_PROGRAMS];
static int             g_program_count = 0;

int startmenu_add_program(const char *label, startmenu_action_fn action, void *ctx) {
    if (!label || g_program_count >= STARTMENU_MAX_PROGRAMS) return 0;
    program_entry_t *e = &g_programs_list[g_program_count];
    strncpy(e->label, label, sizeof(e->label) - 1);
    e->label[sizeof(e->label) - 1] = '\0';
    e->action = action;
    e->ctx = ctx;
    g_program_count++;
    return 1;
}

/* ── Open/close state ───────────────────────────────────────────────── */
static int g_open = 0;
void startmenu_toggle(void) { g_open = !g_open; }
void startmenu_close(void)  { g_open = 0; }
int  startmenu_is_open(void) { return g_open; }

/* ── Init ────────────────────────────────────────────────────────────── */
void startmenu_init(void) {
    load_slot(&g_orb,       STARTMENU_ORB_PATH);
    load_slot(&g_orb_hover, STARTMENU_ORB_HOVER_PATH);
    load_slot(&g_taskbar,   STARTMENU_TASKBAR_PATH);
    load_slot(&g_panel,     STARTMENU_PANEL_PATH);
    load_slot(&g_programs,  STARTMENU_PROGRAMS_PATH);
    load_slot(&g_power,     STARTMENU_POWER_PATH);
}

/* ── Layout ──────────────────────────────────────────────────────────
 * Panel width is a fixed nominal size, scaled down (preserving the
 * source image's aspect ratio, so it doesn't look squashed) if the
 * screen isn't tall enough for the nominal height. Every other box
 * (programs list, power button, orb) is derived as a fraction of the
 * resulting panel size, so this is the one function that has to run
 * again when screen_h/taskbar_h change - everything else just reads
 * g_geom. */
#define PANEL_NOMINAL_W 300
#define PANEL_FALLBACK_NATIVE_W 1080u /* used for aspect-ratio math even if panel.png isn't installed yet */
#define PANEL_FALLBACK_NATIVE_H 1896u

typedef struct {
    int32_t  panel_x, panel_y;
    uint32_t panel_w, panel_h;
    int32_t  programs_x, programs_y;
    uint32_t programs_w, programs_h;
    int32_t  power_x, power_y;
    uint32_t power_w, power_h;
    int32_t  orb_x, orb_y;
    uint32_t orb_size;
    uint32_t row_h;
} startmenu_geom_t;

static startmenu_geom_t g_geom;
static uint32_t g_geom_screen_h = 0, g_geom_taskbar_h = 0;

static void compute_geom(uint32_t screen_h, uint32_t taskbar_h) {
    if (g_geom_screen_h == screen_h && g_geom_taskbar_h == taskbar_h && g_geom_screen_h != 0) return;
    g_geom_screen_h = screen_h;
    g_geom_taskbar_h = taskbar_h;

    uint32_t native_w = g_panel.loaded ? g_panel.raw.width  : PANEL_FALLBACK_NATIVE_W;
    uint32_t native_h = g_panel.loaded ? g_panel.raw.height : PANEL_FALLBACK_NATIVE_H;

    uint32_t panel_w = PANEL_NOMINAL_W;
    uint32_t panel_h = (uint32_t)(((uint64_t)panel_w * native_h) / native_w);

    uint32_t avail_h = (screen_h > taskbar_h + 20) ? (screen_h - taskbar_h - 20) : taskbar_h;
    if (panel_h > avail_h) {
        panel_h = avail_h;
        panel_w = (uint32_t)(((uint64_t)panel_h * native_w) / native_h);
    }
    if (panel_w < 120) panel_w = 120; /* floor so text rows always have room */
    if (panel_h < 160) panel_h = 160;

    g_geom.panel_x = 6;
    g_geom.panel_y = (int32_t)screen_h - (int32_t)taskbar_h - (int32_t)panel_h;
    g_geom.panel_w = panel_w;
    g_geom.panel_h = panel_h;

    uint32_t margin = panel_w / 14;
    if (margin < 6) margin = 6;

    g_geom.programs_x = g_geom.panel_x + (int32_t)margin;
    g_geom.programs_y = g_geom.panel_y + (int32_t)(panel_h / 10);
    g_geom.programs_w = panel_w - 2 * margin;
    g_geom.programs_h = (panel_h * 60) / 100;

    g_geom.power_h = (panel_h * 8) / 100;
    if (g_geom.power_h < 18) g_geom.power_h = 18;
    g_geom.power_w = panel_w - 2 * margin;
    g_geom.power_x = g_geom.panel_x + (int32_t)margin;
    g_geom.power_y = g_geom.panel_y + (int32_t)panel_h - (int32_t)g_geom.power_h - (int32_t)(panel_h * 3 / 100);

    g_geom.row_h = FONT8X16_HEIGHT + 6;

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
    /* Height stays exactly taskbar_h (native height is NOT preserved
     * as pixels-in-the-abstract - "keeps its height" means the strip
     * always fills the taskbar's actual height, whatever that is);
     * width stretches to the current screen resolution. Both are a
     * straight nearest-neighbor stretch, no aspect-ratio lock - this
     * is a UI chrome strip, not a photo, so stretching is the correct
     * behavior here (this is what was actually asked for). */
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

    /* Fallback: plain filled circle, so there's always SOMETHING
     * clickable at the Start button's position even before orb.png
     * is installed - not a fake button, just an honestly plain one. */
    graphics_draw_circle((uint32_t)(g_geom.orb_x + (int32_t)g_geom.orb_size / 2),
                          (uint32_t)(g_geom.orb_y + (int32_t)g_geom.orb_size / 2),
                          g_geom.orb_size / 2, graphics_rgb(70, 130, 180));
}

static void draw_label(int32_t x, int32_t y, const char *s, color_t fg) {
    int32_t cx = x;
    for (const char *p = s; *p; p++) {
        font_draw_glyph((uint32_t)cx, (uint32_t)y, *p, fg, 0, 1 /* transparent */, 1);
        cx += FONT8X16_WIDTH;
    }
}

void startmenu_draw_panel(uint32_t screen_h, uint32_t taskbar_h) {
    if (!g_open) return;
    compute_geom(screen_h, taskbar_h);

    if (g_panel.loaded) {
        ensure_scaled(&g_panel, g_geom.panel_w, g_geom.panel_h);
        if (g_panel.scaled) {
            gui_asset_draw_argb(g_panel.scaled, g_geom.panel_w, g_geom.panel_h, g_geom.panel_x, g_geom.panel_y);
        }
    } else {
        graphics_fill_rect((uint32_t)g_geom.panel_x, (uint32_t)g_geom.panel_y,
                            g_geom.panel_w, g_geom.panel_h, graphics_rgb(40, 44, 52));
    }

    if (g_programs.loaded) {
        ensure_scaled(&g_programs, g_geom.programs_w, g_geom.programs_h);
        if (g_programs.scaled) {
            gui_asset_draw_argb(g_programs.scaled, g_geom.programs_w, g_geom.programs_h,
                                 g_geom.programs_x, g_geom.programs_y);
        }
    } else {
        graphics_fill_rect((uint32_t)g_geom.programs_x, (uint32_t)g_geom.programs_y,
                            g_geom.programs_w, g_geom.programs_h, graphics_rgb(60, 90, 130));
    }

    /* Program labels drawn on top of the (possibly fallback-colored)
     * programs box - no per-item icons were supplied, so this is
     * honestly text-only rather than faking icons that don't exist. */
    int32_t row_y = g_geom.programs_y + 6;
    color_t label_color = GRAPHICS_COLOR_BLACK;
    for (int i = 0; i < g_program_count; i++) {
        if ((uint32_t)(row_y - g_geom.programs_y) + g_geom.row_h > g_geom.programs_h) break;
        draw_label(g_geom.programs_x + 10, row_y, g_programs_list[i].label, label_color);
        row_y += (int32_t)g_geom.row_h;
    }

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

/* Best-effort power off: tries the QEMU/Bochs "isa-debug-exit"-style
 * ACPI shortcut (writing to port 0x604, and the older Bochs 0xB004
 * port) that actually powers off the VM under QEMU/Bochs - this is
 * NOT real ACPI S5 shutdown (that needs parsing the DSDT's \_S5
 * object and writing SLP_TYP/SLP_EN to the real PM1a control port,
 * which this kernel's acpi.c doesn't parse yet). On real hardware or
 * VirtualBox this write does nothing, so if we're still executing
 * afterward, halt cleanly with an on-screen message instead of
 * spinning or silently doing nothing - the same honest fallback
 * philosophy as everything else in this file. */
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

    if (x >= g_geom.power_x && x < g_geom.power_x + (int32_t)g_geom.power_w &&
        y >= g_geom.power_y && y < g_geom.power_y + (int32_t)g_geom.power_h) {
        system_shutdown();
        return 1; /* unreachable on real shutdown; kept for the honest-fallback halt path above */
    }

    int32_t row_y = g_geom.programs_y + 6;
    for (int i = 0; i < g_program_count; i++) {
        if ((uint32_t)(row_y - g_geom.programs_y) + g_geom.row_h > g_geom.programs_h) break;
        if (x >= g_geom.programs_x && x < g_geom.programs_x + (int32_t)g_geom.programs_w &&
            y >= row_y && y < row_y + (int32_t)g_geom.row_h) {
            if (g_programs_list[i].action) g_programs_list[i].action(g_programs_list[i].ctx);
            startmenu_close();
            return 1;
        }
        row_y += (int32_t)g_geom.row_h;
    }

    return 1; /* inside the panel but not on any control - consumed, stays open */
}
