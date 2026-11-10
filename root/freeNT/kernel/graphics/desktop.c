
#include "desktop.h"
#include "graphics_core.h"
#include "graphics_2d.h"
#include "font8x16.h"
#include "cursor.h"
#include "mouse.h"
#include "keyboard.h"
#include "pit.h"
#include "rtc.h"
#include "gfx_terminal.h"
#include "io.h"
#include "string.h"
#include "wm.h"
#include "desktop_menu.h"
#include "wallpaper.h"
#include "startmenu.h"
#include "process.h"
#include "apphost.h"
#include "kernel.h"
#include "crash.h"
#include "gui_asset.h"
#include "heap.h"
extern const char *osimage_status(void);
#include "loader.h"     /* loader_load_executable */
#include "fs.h"         /* fs_stat - check the file actually exists before trying to run it */

#define TASKBAR_H       32
#define CHROME_BTN_SIZE 16
#define CHROME_BTN_GAP  4

/* On-screen feedback for menu actions that can fail (launching a
 * .trp that isn't on disk) - there's no visible console while the
 * desktop owns the whole screen, so silently failing would look
 * exactly like the button doing nothing at all. Shown in the
 * taskbar for a couple seconds, then cleared automatically. */

/* action_change_wallpaper() (below) can't reach desktop_run()'s local
 * `active_menu`/`wallpaper_picker_menu` directly - menu actions are
 * plain function pointers with no closure over the caller's locals.
 * This flag is the handoff: the action sets it, the main loop (which
 * DOES own those locals) checks it once per iteration and actually
 * builds+opens the picker there. Same reasoning as
 * g_now_ms_for_menu_actions below. */
static int g_open_wallpaper_picker = 0;


/* ── Window chrome colors ─────────────────────────────────────────── */
static color_t chrome_titlebar_focused(void)   { return graphics_rgb(0x2A, 0x5C, 0x8A); }
static color_t chrome_titlebar_unfocused(void) { return graphics_rgb(0x50, 0x50, 0x54); }
static color_t chrome_border(void)             { return graphics_rgb(0x18, 0x18, 0x1A); }
static color_t chrome_close_btn(void)          { return graphics_rgb(0xC0, 0x30, 0x30); }
static color_t chrome_minmax_btn(void)         { return graphics_rgb(0x48, 0x48, 0x4C); }

/* ── Small text helper (mirrors the previous desktop.c's draw_string,
 * kept for the taskbar clock / hints - window chrome text below uses
 * font_draw_glyph directly since it needs per-glyph positioning for
 * the titlebar's fixed layout). ────────────────────────────────────*/
static void draw_string(uint32_t x, uint32_t y, const char *s, color_t fg,
                        color_t bg, int transparent, uint32_t scale) {
    uint32_t cx = x;
    for (const char *p = s; *p; p++) {
        font_draw_glyph(cx, y, *p, fg, bg, transparent, scale);
        cx += FONT8X16_WIDTH * scale;
    }
}

static void draw_two_digit(uint32_t x, uint32_t y, uint8_t v, color_t fg, color_t bg) {
    char buf[3];
    buf[0] = (char)('0' + (v / 10) % 10);
    buf[1] = (char)('0' + v % 10);
    buf[2] = '\0';
    draw_string(x, y, buf, fg, bg, 0, 1);
}

static int point_in_rect(int32_t px, int32_t py, int32_t x, int32_t y,
                         int32_t w, int32_t h) {
    return px >= x && px < x + w && py >= y && py < y + h;
}


static void chrome_button_rects(wm_window_t *win, gfx2d_rect_t *out_close,
                                gfx2d_rect_t *out_maximize, gfx2d_rect_t *out_minimize) {
    int32_t y = win->y + (WM_TITLEBAR_HEIGHT - CHROME_BTN_SIZE) / 2;
    int32_t right_edge = win->x + win->w - WM_BORDER_WIDTH - CHROME_BTN_GAP;

    int32_t close_x = right_edge - CHROME_BTN_SIZE;
    int32_t max_x    = close_x - CHROME_BTN_GAP - CHROME_BTN_SIZE;
    int32_t min_x    = max_x - CHROME_BTN_GAP - CHROME_BTN_SIZE;

    out_close->x = close_x;    out_close->y = y;
    out_close->w = CHROME_BTN_SIZE; out_close->h = CHROME_BTN_SIZE;

    out_maximize->x = max_x;   out_maximize->y = y;
    out_maximize->w = CHROME_BTN_SIZE; out_maximize->h = CHROME_BTN_SIZE;

    out_minimize->x = min_x;   out_minimize->y = y;
    out_minimize->w = CHROME_BTN_SIZE; out_minimize->h = CHROME_BTN_SIZE;
}

/* ── Chrome + blit: composites one window onto the screen ──────────── */
static void draw_window(wm_window_t *win) {
    int focused = (win->flags & WM_WIN_FOCUSED) != 0;
    color_t titlebar = focused ? chrome_titlebar_focused() : chrome_titlebar_unfocused();
    color_t border   = chrome_border();

    gfx2d_rect_t frame = { win->x, win->y, (uint32_t)win->w, (uint32_t)win->h };
    gfx2d_fill_rect(frame, border);

    gfx2d_rect_t bar = { win->x + WM_BORDER_WIDTH, win->y,
                          (uint32_t)(win->w - 2 * WM_BORDER_WIDTH),
                          (uint32_t)WM_TITLEBAR_HEIGHT };
    gfx2d_fill_rect(bar, titlebar);

    uint32_t text_y = (uint32_t)(win->y + (WM_TITLEBAR_HEIGHT - FONT8X16_HEIGHT) / 2);
    uint32_t text_x = (uint32_t)(win->x + WM_BORDER_WIDTH + 6);
    for (const char *p = win->title; *p; p++) {
        font_draw_glyph(text_x, text_y, *p, GRAPHICS_COLOR_WHITE, titlebar, 1, 1);
        text_x += FONT8X16_WIDTH;
    }

    gfx2d_rect_t close_rect, max_rect, min_rect;
    chrome_button_rects(win, &close_rect, &max_rect, &min_rect);

    gfx2d_fill_rect(close_rect, chrome_close_btn());
    font_draw_glyph((uint32_t)close_rect.x + 4, (uint32_t)close_rect.y, 'x',
                     GRAPHICS_COLOR_WHITE, chrome_close_btn(), 1, 1);

    gfx2d_fill_rect(max_rect, chrome_minmax_btn());
    /* '+' when not maximized (click to maximize), a small box glyph
     * substitute ('=') when already maximized (click to restore) -
     * real desktops draw two overlapping squares for "restore"; a
     * plain glyph is the honest simple version rather than hand-
     * drawing a second icon shape pixel-by-pixel for one button. */
    font_draw_glyph((uint32_t)max_rect.x + 4, (uint32_t)max_rect.y,
                     (win->flags & WM_WIN_MAXIMIZED) ? '=' : '+',
                     GRAPHICS_COLOR_WHITE, chrome_minmax_btn(), 1, 1);

    gfx2d_fill_rect(min_rect, chrome_minmax_btn());
    font_draw_glyph((uint32_t)min_rect.x + 4, (uint32_t)min_rect.y, '_',
                     GRAPHICS_COLOR_WHITE, chrome_minmax_btn(), 1, 1);

    /* Client area: blit the window's own pixel buffer. wm_window_t's
     * pixels/client_w/client_h are exactly the shape gfx2d_surface_t
     * expects (see wm.h's header comment), so this is a zero-copy
     * view, not a duplicate buffer. */
    gfx2d_surface_t view;
    view.width  = (uint32_t)win->client_w;
    view.height = (uint32_t)win->client_h;
    view.pixels = (color_t *)win->pixels;
    gfx2d_blit(&view, win->client_x, win->client_y);
}

/* Which button (if any) is at screen point (x,y). Returns 'c' (close),
 * 'M' (maximize), 'm' (minimize), or 0 (none) - checked in that order
 * since the buttons don't overlap, order doesn't affect correctness,
 * just avoids three separate point_in_rect() call sites at every use. */
static char chrome_button_at(wm_window_t *win, int32_t x, int32_t y) {
    gfx2d_rect_t close_rect, max_rect, min_rect;
    chrome_button_rects(win, &close_rect, &max_rect, &min_rect);

    if (point_in_rect(x, y, close_rect.x, close_rect.y, (int32_t)close_rect.w, (int32_t)close_rect.h)) return 'c';
    if (point_in_rect(x, y, max_rect.x, max_rect.y, (int32_t)max_rect.w, (int32_t)max_rect.h)) return 'M';
    if (point_in_rect(x, y, min_rect.x, min_rect.y, (int32_t)min_rect.w, (int32_t)min_rect.h)) return 'm';
    return 0;
}

/* ── Right-click context menu actions ───────────────────────────────
 * menu_action_fn's `ctx` carries whatever the menu was opened
 * against - for the desktop-empty-space menu that's NULL (no window
 * involved); for the window-titlebar menu it's the wm_window_t* that
 * was right-clicked, passed at the menu_handle_click() call site.
 *
 * "New Terminal" and "Settings" do NOT spawn a fake built-in window -
 * they attempt to actually load and run terminal.trp / settings.trp
 * from disk, the exact same way the shell's own `run <path>` command
 * does (see root/ToriginalOS/shell/shell.c's cmd_run(): process_create() then
 * loader_load_executable() then process_start()). Neither .trp
 * exists on disk yet (root/apps/term/term.c is real source but isn't
 * built into a .trp and installed onto the filesystem image as part
 * of this change - that's a packaging/build step, not a kernel-side
 * one), so today both buttons will genuinely report "not found" via
 * set_status_message() rather than silently doing nothing or opening
 * a placeholder window pretending to be a real app. The moment a real
 * terminal.trp/settings.trp is placed on disk, these start working
 * with zero changes needed here. */
static uint64_t g_now_ms_for_menu_actions = 0; /* set once per frame before dispatching a menu click */

static uint64_t g_busy_until = 0;

static void launch_trp(const char *path) {
    g_busy_until = g_now_ms_for_menu_actions + 800;
    if (apphost_launch(path) < 0) gui_error_report("Could not start program", path);
}

static void action_new_terminal(void *ctx) {
    (void)ctx;
    launch_trp("/system/trp/terminal.trp");
}

static void action_computer(void *ctx) {
    (void)ctx;
    launch_trp("/system/trp/computer.trp");
}

static void action_welcome(void *ctx) {
    (void)ctx;
    launch_trp("/system/trp/welcome.trp");
}

static void action_notepad(void *ctx) {
    (void)ctx;
    launch_trp("/system/trp/notepad.trp");
}

static void action_packages(void *ctx) {
    (void)ctx;
    launch_trp("/system/trp/packages.trp");
}

static void action_settings(void *ctx) {
    (void)ctx;
    launch_trp("/system/trp/settings.trp");
}

#define DI_SZ   48
#define DI_CELL_W 88
#define DI_CELL_H 92
#define DI_COUNT 6

typedef struct { const char *label, *trp, *icon; color_t *img; } dicon_t;

static dicon_t g_dicons[DI_COUNT] = {
    { "Computer", "/system/trp/computer.trp", "/system/gui/icons/vista_pc_1.png", NULL },
    { "Terminal", "/system/trp/terminal.trp", "/system/gui/icons/terminal.png", NULL },
    { "Notepad",  "/system/trp/notepad.trp",  "/system/gui/icons/notepad.png", NULL },
    { "Settings", "/system/trp/settings.trp", "/system/gui/icons/vista_personalization.png", NULL },
    { "Packages", "/system/trp/packages.trp", "/system/gui/icons/vista_marketplace.png", NULL },
    { "Welcome",  "/system/trp/welcome.trp",  "/system/gui/icons/vista_get_started.png", NULL },
};
static color_t *g_tbicons[DI_COUNT];
static int g_di_sel = -1;
static uint64_t g_last_mode_ms = 0;
static uint64_t g_di_last = 0;

static void di_load(void) {
    for (int i = 0; i < DI_COUNT; i++) {
        png_image_t raw;
        g_dicons[i].img = NULL;
        g_tbicons[i] = NULL;
        if (!gui_asset_load_png(g_dicons[i].icon, &raw)) continue;
        g_dicons[i].img = gui_asset_scale_argb(&raw, DI_SZ, DI_SZ);
        g_tbicons[i] = gui_asset_scale_argb(&raw, 20, 20);
        png_free(&raw);
    }
}

static void di_free(void) {
    for (int i = 0; i < DI_COUNT; i++) {
        if (g_dicons[i].img) kfree(g_dicons[i].img);
        g_dicons[i].img = NULL;
        if (g_tbicons[i]) kfree(g_tbicons[i]);
        g_tbicons[i] = NULL;
    }
}

static int has_word(const char *h, const char *n) {
    size_t nl = strlen(n);
    for (; *h; h++) if (strncmp(h, n, nl) == 0) return 1;
    return 0;
}

static int tb_icon_for(const char *title) {
    for (int i = 0; i < DI_COUNT; i++)
        if (g_tbicons[i] && has_word(title, g_dicons[i].label)) return i;
    return -1;
}

static void di_pos(int i, uint32_t h, int32_t *x, int32_t *y) {
    int per = ((int)h - TASKBAR_H - 16) / DI_CELL_H;
    if (per < 1) per = 1;
    *x = 12 + (i / per) * DI_CELL_W;
    *y = 12 + (i % per) * DI_CELL_H;
}

static int di_hit(uint32_t h, int32_t mx, int32_t my) {
    for (int i = 0; i < DI_COUNT; i++) {
        int32_t x, y;
        di_pos(i, h, &x, &y);
        if (mx >= x && mx < x + DI_CELL_W && my >= y && my < y + DI_CELL_H) return i;
    }
    return -1;
}

static void di_draw(uint32_t h) {
    for (int i = 0; i < DI_COUNT; i++) {
        int32_t x, y;
        di_pos(i, h, &x, &y);
        if (i == g_di_sel) {
            gfx2d_rect_t r = { x + 2, y + 2, DI_CELL_W - 4, DI_CELL_H - 4 };
            gfx2d_fill_rect(r, graphics_rgb(48, 96, 150));
        }
        if (g_dicons[i].img)
            gui_asset_draw_argb(g_dicons[i].img, DI_SZ, DI_SZ, x + (DI_CELL_W - DI_SZ) / 2, y + 8);
        int tw = (int)strlen(g_dicons[i].label) * FONT8X16_WIDTH;
        int tx = x + (DI_CELL_W - tw) / 2;
        int ty = y + 8 + DI_SZ + 6;
        draw_string((uint32_t)tx + 1, (uint32_t)ty + 1, g_dicons[i].label, graphics_rgb(0, 0, 0), 0, 1, 1);
        draw_string((uint32_t)tx, (uint32_t)ty, g_dicons[i].label, GRAPHICS_COLOR_WHITE, 0, 1, 1);
    }
}

#define PRI_DIR "/system/pritxt"
#define PRI_MAX 12
#define PRI_LEN 96

static char g_pri[PRI_MAX][PRI_LEN];
static int g_npri;
static char g_pri_files[8][64];
static int g_npri_files;

static int pri_collect(const char *name, uint8_t name_len, uint8_t type, void *ctx) {
    (void)ctx;
    if (type == FILE_TYPE_DIR || name_len < 8 || name_len >= 64 || g_npri_files >= 8) return 0;
    if (strcmp(name + name_len - 7, ".pritxt") != 0) return 0;
    memcpy(g_pri_files[g_npri_files], name, name_len);
    g_pri_files[g_npri_files][name_len] = 0;
    g_npri_files++;
    return 0;
}

static void pri_load(void) {
    g_npri = 0;
    g_npri_files = 0;
    if (fs_readdir(PRI_DIR, pri_collect, NULL) != 0) return;
    for (int f = 0; f < g_npri_files; f++) {
        char path[128];
        size_t dl = strlen(PRI_DIR);
        memcpy(path, PRI_DIR, dl);
        path[dl] = '/';
        strncpy(path + dl + 1, g_pri_files[f], sizeof(path) - dl - 2);
        path[sizeof(path) - 1] = 0;
        fd_t fd = fs_open(path, O_RDONLY, 0);
        if (fd < 0) continue;
        char buf[512];
        ssize_t n = fs_read(fd, buf, sizeof(buf) - 1);
        fs_close(fd);
        if (n <= 0) continue;
        buf[n] = 0;
        char *p = buf;
        while (*p && g_npri < PRI_MAX) {
            char *e = p;
            while (*e && *e != '\n' && *e != '\r') e++;
            size_t l = (size_t)(e - p);
            if (l >= PRI_LEN) l = PRI_LEN - 1;
            if (l) { memcpy(g_pri[g_npri], p, l); g_pri[g_npri][l] = 0; g_npri++; }
            while (*e == '\n' || *e == '\r') e++;
            p = e;
        }
    }
}

static void pri_draw(uint32_t w, uint32_t h) {
    uint32_t y = h - TASKBAR_H - 10 - (uint32_t)g_npri * (FONT8X16_HEIGHT + 2);
    for (int i = 0; i < g_npri; i++) {
        uint32_t tw = (uint32_t)strlen(g_pri[i]) * FONT8X16_WIDTH;
        uint32_t x = w > tw + 14 ? w - tw - 14 : 0;
        draw_string(x + 1, y + 1, g_pri[i], graphics_rgb(0, 0, 0), 0, 1, 1);
        draw_string(x, y, g_pri[i], GRAPHICS_COLOR_WHITE, 0, 1, 1);
        y += FONT8X16_HEIGHT + 2;
    }
}

static void action_change_wallpaper(void *ctx) {
    (void)ctx;
    g_open_wallpaper_picker = 1; /* handled in desktop_run()'s loop - see the flag's declaration above */
}

static void action_pick_wallpaper(void *ctx) {
    const char *path = (const char *)ctx;
    if (wallpaper_set_path(path)) {
    } else {
    }
}

static void close_window_any(wm_window_t *win) {
    if (win->flags & WM_WIN_USERBACKED) apphost_close_window(win->id);
    else wm_destroy_window(win->id);
}

static void action_close_window(void *ctx) {
    wm_window_t *win = (wm_window_t *)ctx;
    if (win) close_window_any(win);
}

static void action_minimize_window(void *ctx) {
    wm_window_t *win = (wm_window_t *)ctx;
    if (win) wm_minimize_window(win->id);
}

static void action_maximize_window(void *ctx) {
    wm_window_t *win = (wm_window_t *)ctx;
    if (!win) return;
    if (win->flags & WM_WIN_MAXIMIZED) wm_restore_window(win->id);
    else wm_maximize_window(win->id);
}

/* ── Main loop ────────────────────────────────────────────────────── */

static volatile uint32_t g_pend_w = 0, g_pend_h = 0;
static int g_desktop_up = 0;

int desktop_is_running(void) { return g_desktop_up; }

int desktop_request_resolution(uint32_t w, uint32_t h) {
    if (!g_desktop_up) return 0;
    g_pend_h = h;
    g_pend_w = w;
    return 1;
}

void desktop_run(void) {
    if (!graphics_is_available()) {
        io_put_string("desktop: no graphics framebuffer available; use the shell instead.\n");
        return;
    }

    uint32_t w = g_framebuffer.width;
    uint32_t h = g_framebuffer.height;

    wallpaper_init(); /* loads whatever WALLPAPER_CFG_PATH points at, if anything - purely file-driven, see wallpaper.c */
    startmenu_init(); /* loads orb/taskbar/menu_bg/programs_block/shortcut/power PNGs if installed - see startmenu.c */
    startmenu_set_launcher(launch_trp);

    wm_init((int32_t)w, (int32_t)h);
    /* Maximized windows fill the screen minus the taskbar - not the
     * whole screen, or "maximize" would draw a window on top of (and
     * make unreachable) the taskbar's clock and the only way back to
     * a minimized window. */
    wm_set_maximize_area(0, 0, (int32_t)w, (int32_t)(h - TASKBAR_H));

    /* One demo window so the WM has something to demonstrate drag/
     * resize/minimize/maximize/close on - see paint_demo_window_content()'s
     * comment: nothing real runs inside a WM window yet (term.c runs
     * full-screen, not windowed), so this is honestly labeled as a
     * demo rather than pretending to be a real app. There is
     * deliberately no "New Window" menu item spawning more of these -
     * the right-click menu only offers things that attempt to launch
     * a real .trp (New Terminal, Settings). */
    desktop_menu_t empty_space_menu;
    menu_init(&empty_space_menu);
    menu_add_item(&empty_space_menu, "Change Wallpaper", action_change_wallpaper);

    /* Start menu's program list reuses the exact same actions as the
     * right-click menu - two entry points to the same real launch
     * logic (launch_trp()), not two separate implementations that
     * could drift apart. */
    startmenu_add_program("New Terminal", action_new_terminal, NULL);
    startmenu_add_program("Computer", action_computer, NULL);
    startmenu_add_program("Welcome", action_welcome, NULL);
    startmenu_add_program("Notepad", action_notepad, NULL);
    startmenu_add_program("Settings", action_settings, NULL);
    startmenu_add_program("Packages", action_packages, NULL);
    startmenu_add_program("Change Wallpaper", action_change_wallpaper, NULL);

    desktop_menu_t window_menu;
    menu_init(&window_menu);
    menu_add_item(&window_menu, "Minimize", action_minimize_window);
    menu_add_item(&window_menu, "Maximize", action_maximize_window);
    menu_add_item(&window_menu, "Close Window", action_close_window);

    /* Built fresh every time it's opened (see the g_open_wallpaper_picker
     * handling below) by scanning WALLPAPER_DIR - never a fixed list.
     * g_wallpaper_menu_paths holds the full path for each row since
     * menu_item_t.label is display-only; item_ctx points into this
     * array so action_pick_wallpaper() gets the real path to load. */
    desktop_menu_t wallpaper_picker_menu;
    menu_init(&wallpaper_picker_menu);
    char g_wallpaper_menu_paths[MENU_MAX_ITEMS][256];

    /* Which menu is open, and (for window_menu specifically) which
     * window it targets - menu_handle_click()'s ctx argument is
     * supplied by the CALLER at click time, not stored on the menu
     * itself, so the target window has to be tracked here. */
    desktop_menu_t *active_menu = NULL;
    wm_window_t *active_menu_target = NULL;

    mouse_set_bounds((int32_t)w, (int32_t)h);
    di_load();
    pri_load();

    uint64_t last_frame = 0;
    int running = 1;
    int last_left = 0;
    int last_right = 0;

    g_desktop_up = 1;
    while (running) {
        uint64_t now = pit_get_milliseconds();
        if (g_pend_w && (now - g_last_mode_ms >= 400 || g_last_mode_ms == 0)) {
            uint32_t nw = g_pend_w, nh = g_pend_h;
            g_pend_w = 0;
            if (nw != w || nh != h) {
                g_last_mode_ms = now ? now : 1;
                wallpaper_release_scaled();
                if (graphics_set_mode(GRAPHICS_MODE_VESA_32BIT, nw, nh) == 0) {
                    w = g_framebuffer.width;
                    h = g_framebuffer.height;
                    wm_set_screen((int32_t)w, (int32_t)h);
                    wm_set_maximize_area(0, 0, (int32_t)w, (int32_t)(h - TASKBAR_H));
                    mouse_set_bounds((int32_t)w, (int32_t)h);
                    wm_window_t *wl; int wc;
                    wm_get_window_list(&wl, &wc);
                    uint32_t ids[64]; int ni = 0;
                    for (int i = 0; i < wc && ni < 64; i++)
                        if (wl[i].flags & WM_WIN_MAXIMIZED) ids[ni++] = wl[i].id;
                    for (int i = 0; i < ni; i++) { wm_restore_window(ids[i]); wm_maximize_window(ids[i]); }
                } else {
                    gui_error_report("Resolution change failed", "The display adapter rejected that size. Nothing was changed.");
                }
            }
        }
        g_now_ms_for_menu_actions = now;

        apphost_step();
        gui_error_draw(w, h, TASKBAR_H);

        while (keyboard_has_input()) {
            char c = keyboard_getc_nb();
            uint32_t fid = wm_get_focus();
            wm_window_t *fw = fid ? wm_get_window(fid) : NULL;
            if (fw && (fw->flags & WM_WIN_USERBACKED)) wm_handle_key((unsigned char)c, 1);
            else if (c == 27) running = 0;
        }

        mouse_state_t st;
        mouse_get_state(&st);

        int left_pressed  = st.left_button  && !last_left;
        int right_pressed = st.right_button && !last_right;

        int startmenu_consumed = 0;
        if (left_pressed) {
            if (startmenu_orb_hit(h, TASKBAR_H, st.x, st.y)) {
                startmenu_toggle();
                startmenu_consumed = 1;
            } else if (startmenu_is_open()) {
                /* startmenu_handle_click() itself closes the menu on
                 * a click outside the panel and returns 0 in that
                 * case - that click should still fall through to the
                 * normal window/chrome handling below (matching real
                 * desktops: clicking elsewhere both dismisses the
                 * start menu AND acts on whatever was actually
                 * clicked), so only a genuine in-panel hit is treated
                 * as consumed here. */
                startmenu_consumed = startmenu_handle_click(h, TASKBAR_H, st.x, st.y);
            }
        }

        if (!startmenu_consumed) {
        if (active_menu && active_menu->open && left_pressed) {
            menu_handle_click(active_menu, st.x, st.y, active_menu_target);
            menu_close(active_menu);
            active_menu = NULL;
            active_menu_target = NULL;
        } else if (right_pressed) {
            if (active_menu && active_menu->open) {
                menu_close(active_menu);
                active_menu = NULL;
                active_menu_target = NULL;
            }

            /* wm_handle_mouse_down(button=1) deliberately doesn't
             * raise/focus/drag (see wm.c's comment) - it purely
             * reports what was right-clicked, which is exactly what's
             * needed to decide which menu to show. */
            uint32_t hit_id = wm_handle_mouse_down(st.x, st.y, 1);
            if (hit_id != 0) {
                wm_window_t *hit = wm_get_window(hit_id);
                int32_t rel_y = st.y - (hit ? hit->y : 0);
                if (hit && rel_y < WM_TITLEBAR_HEIGHT) {
                    menu_open_at(&window_menu, st.x, st.y);
                    active_menu = &window_menu;
                    active_menu_target = hit;
                }
                /* Right-clicking a window's client content (rather
                 * than its titlebar) has no menu today - that would
                 * be an app-defined context menu, which needs an app
                 * running inside the window to define it (see
                 * paint_demo_window_content()'s comment: nothing
                 * real runs inside these windows yet). */
            } else {
                menu_open_at(&empty_space_menu, st.x, st.y);
                active_menu = &empty_space_menu;
                active_menu_target = NULL;
            }
        } else if (left_pressed) {
            /* Chrome button hit-test happens BEFORE handing off to
             * wm_handle_mouse_down(), since clicking close/minimize/
             * maximize shouldn't also raise/focus/start-dragging the
             * window underneath the button. Only visible (non-
             * minimized) windows can have their buttons clicked, for
             * the obvious reason that a minimized window's titlebar
             * isn't on screen to click. */
            wm_window_t *list; int count;
            wm_get_window_list(&list, &count);
            int consumed = 0;
            for (int i = count - 1; i >= 0; i--) {
                if (list[i].flags & WM_WIN_MINIMIZED) continue;
                if (!point_in_rect(st.x, st.y, list[i].x, list[i].y, list[i].w, list[i].h)) continue;

                char btn = chrome_button_at(&list[i], st.x, st.y);
                if (btn == 'c') {
                    close_window_any(&list[i]);
                    consumed = 1;
                } else if (btn == 'M') {
                    if (list[i].flags & WM_WIN_MAXIMIZED) wm_restore_window(list[i].id);
                    else wm_maximize_window(list[i].id);
                    consumed = 1;
                } else if (btn == 'm') {
                    wm_minimize_window(list[i].id);
                    consumed = 1;
                }
                if (consumed) break;
            }
            if (!consumed) {
                uint32_t hid = wm_handle_mouse_down(st.x, st.y, 0);
                if (hid == 0) {
                    int di = di_hit(h, st.x, st.y);
                    if (di >= 0 && di == g_di_sel && now - g_di_last < 500) {
                        g_di_sel = -1;
                        launch_trp(g_dicons[di].trp);
                    } else {
                        g_di_sel = di;
                        g_di_last = now;
                    }
                } else {
                    g_di_sel = -1;
                }
            }
        } else if (st.left_button) {
            wm_handle_mouse_move(st.x, st.y);
        }
        } /* !startmenu_consumed */

        /* Checked unconditionally (not nested inside the branch
         * above) because action_change_wallpaper() can be triggered
         * from either the right-click menu OR the start menu's
         * program list - two separate click-handling paths above -
         * and this needs to fire regardless of which one set it. */
        if (g_open_wallpaper_picker) {
            g_open_wallpaper_picker = 0;

            char names[MENU_MAX_ITEMS][128];
            int count = wallpaper_list(names, MENU_MAX_ITEMS);

            menu_init(&wallpaper_picker_menu);
            if (count == 0) {
                menu_add_item(&wallpaper_picker_menu,
                              "(no wallpapers in " WALLPAPER_DIR ")", NULL);
            } else {
                for (int i = 0; i < count; i++) {
                    size_t dl = strlen(WALLPAPER_DIR);
                    memcpy(g_wallpaper_menu_paths[i], WALLPAPER_DIR, dl);
                    g_wallpaper_menu_paths[i][dl] = '/';
                    strncpy(g_wallpaper_menu_paths[i] + dl + 1, names[i],
                            sizeof(g_wallpaper_menu_paths[i]) - dl - 2);
                    g_wallpaper_menu_paths[i][sizeof(g_wallpaper_menu_paths[i]) - 1] = '\0';

                    menu_add_item_ctx(&wallpaper_picker_menu, names[i],
                                      action_pick_wallpaper, g_wallpaper_menu_paths[i]);
                }
            }
            menu_open_at(&wallpaper_picker_menu, st.x, st.y);
            active_menu = &wallpaper_picker_menu;
            active_menu_target = NULL;
        }

        if (!st.left_button && last_left) {
            wm_handle_mouse_up(st.x, st.y, 0);
        }

        last_left = st.left_button;
        last_right = st.right_button;

        if (running && (now - last_frame >= 33 || last_frame == 0)) {
            /* Real file-driven wallpaper if one is loaded (see
             * wallpaper.c); the flat navy fill is only the fallback
             * for "nothing installed at WALLPAPER_DIR yet", not the
             * default look. */
            wallpaper_draw(w, h, graphics_rgb(24, 58, 82));
            di_draw(h);
            pri_draw(w, h);

            wm_window_t *list; int count;
            wm_get_window_list(&list, &count);
            for (int i = 0; i < count; i++) {
                /* Minimized windows are skipped entirely - not drawn,
                 * not composited, matching WM_WIN_MINIMIZED's meaning
                 * (see wm.h's comment: "hidden from compositing, still
                 * exists/tracked"). They're still in the list, so
                 * clicking their taskbar entry below can restore them. */
                if (list[i].flags & WM_WIN_MINIMIZED) continue;
                draw_window(&list[i]);
            }

            color_t taskbar = graphics_rgb(14, 18, 24);
            color_t accent  = graphics_rgb(90, 190, 210);
            color_t white   = GRAPHICS_COLOR_WHITE;

            /* Image-backed taskbar strip (see startmenu.c) - width
             * always matches the current resolution, height always
             * matches TASKBAR_H, falls back to the flat `taskbar`
             * color if taskbar.png isn't installed yet. */
            startmenu_draw_taskbar_bg(w, h, TASKBAR_H, taskbar);
            graphics_draw_line(0, h - TASKBAR_H, w - 1, h - TASKBAR_H, accent);

            startmenu_draw_orb(h, TASKBAR_H, st.x, st.y);

            /* Taskbar left side: either the status message from a
             * recent menu action (launched/not-found/etc.), or the
             * default hint text - never both, so a real result isn't
             * immediately buried under static help text. Starts after
             * the orb button (TASKBAR_H + 12px reserved) so the text
             * never overlaps it. */


            /* Minimized windows get a small clickable label in the
             * taskbar - without this, minimizing a window would be a
             * one-way trip (see wm_restore_window()'s existence: the
             * WM-level API to bring one back exists, but needs SOME
             * on-screen affordance to trigger it, since the window
             * itself isn't visible to click on anymore). */
            uint32_t taskbar_item_x = TASKBAR_H + 10;
            for (int i = 0; i < count; i++) {
                if (!(list[i].flags & WM_WIN_MINIMIZED)) continue;
                char tbuf[20];
                size_t tl = strlen(list[i].title);
                if (tl > 18) tl = 18;
                memcpy(tbuf, list[i].title, tl);
                tbuf[tl] = 0;
                int ti = tb_icon_for(list[i].title);
                uint32_t iw = ti >= 0 ? 26 : 0;
                uint32_t label_w = iw + (uint32_t)(tl + 2) * FONT8X16_WIDTH;
                gfx2d_rect_t item = { (int32_t)taskbar_item_x, (int32_t)(h - TASKBAR_H + 4),
                                       label_w, TASKBAR_H - 8 };
                gfx2d_fill_rect(item, graphics_rgb(40, 46, 54));
                if (ti >= 0)
                    gui_asset_draw_argb(g_tbicons[ti], 20, 20, (int32_t)taskbar_item_x + 6,
                                        (int32_t)(h - TASKBAR_H + (TASKBAR_H - 20) / 2));
                draw_string(taskbar_item_x + iw + FONT8X16_WIDTH, h - TASKBAR_H + (TASKBAR_H - FONT8X16_HEIGHT) / 2,
                            tbuf, white, graphics_rgb(40, 46, 54), 0, 1);
                taskbar_item_x += label_w + 8;
            }

            rtc_time_t t;
            rtc_read(&t);
            uint32_t clock_y = h - TASKBAR_H + (TASKBAR_H - FONT8X16_HEIGHT) / 2;
            uint32_t clock_x = w - 12 - (8 * FONT8X16_WIDTH);
            draw_two_digit(clock_x, clock_y, t.hour, white, taskbar);
            draw_string(clock_x + 2 * FONT8X16_WIDTH, clock_y, ":", white, taskbar, 1, 1);
            draw_two_digit(clock_x + 3 * FONT8X16_WIDTH, clock_y, t.minute, white, taskbar);
            draw_string(clock_x + 5 * FONT8X16_WIDTH, clock_y, ":", white, taskbar, 1, 1);
            draw_two_digit(clock_x + 6 * FONT8X16_WIDTH, clock_y, t.second, white, taskbar);

            if (active_menu) menu_draw(active_menu, st.x, st.y);

            startmenu_draw_panel(h, TASKBAR_H);

            cursor_shape_t shape = st.left_button ? CURSOR_HAND : CURSOR_ARROW;
            int hint = wm_cursor_hint(st.x, st.y);
            if (hint == 1) shape = CURSOR_RESIZE_NS;
            else if (hint == 2) shape = CURSOR_RESIZE_WE;
            else if (hint == 3) shape = CURSOR_RESIZE_NWSE;
            else if (hint == 4) shape = CURSOR_RESIZE_NESW;
            else if (hint == 5) shape = CURSOR_MOVE;
            else if (now < g_busy_until) shape = CURSOR_BUSY;
            cursor_draw(st.x, st.y, shape, GRAPHICS_COLOR_WHITE);

            /* The actual flicker fix: every draw call above wrote to
             * an off-screen back buffer (see graphics_core.c's
             * graphics_init()), not the real screen - this is the
             * ONE place per frame anything becomes visible, in a
             * single bulk copy instead of dozens of individual
             * fill_rect/blit calls each being scanned out mid-draw. */
            graphics_present();

            last_frame = now;
        }

        /* Clicking a minimized window's taskbar label restores it -
         * checked after the frame's draw pass computed taskbar_item_x
         * positions would be ideal, but those are frame-local; instead
         * this re-derives the same layout on click, which is cheap
         * (at most WM_MAX_WINDOWS iterations) and keeps the hit-test
         * independent of exactly when in the loop a click lands
         * relative to the throttled ~30fps draw. */
        if (left_pressed && st.y >= (int32_t)(h - TASKBAR_H) && st.x >= (int32_t)(TASKBAR_H + 10)) {
            wm_window_t *list; int count;
            wm_get_window_list(&list, &count);
            uint32_t item_x = TASKBAR_H + 10;
            for (int i = 0; i < count; i++) {
                if (!(list[i].flags & WM_WIN_MINIMIZED)) continue;
                size_t tl = strlen(list[i].title);
                if (tl > 18) tl = 18;
                uint32_t label_w = (tb_icon_for(list[i].title) >= 0 ? 26u : 0u) + (uint32_t)(tl + 2) * FONT8X16_WIDTH;
                if ((uint32_t)st.x >= item_x && (uint32_t)st.x < item_x + label_w) {
                    wm_restore_window(list[i].id);
                    break;
                }
                item_x += label_w + 8;
            }
        }
    }

    di_free();
    apphost_shutdown_all();
    g_desktop_up = 0;
    wm_shutdown();

    gterm_clear();
    io_put_string("Back in the shell. Type 'help' for a list of commands.\n");
}
